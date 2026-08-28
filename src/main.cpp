#include <cstdio>
#include <cstring>
#include <csignal>
#include <string>
#include <print>
#include <algorithm>
#include <cctype>
#include <unordered_map>

#include <unistd.h>
#include <sys/wait.h>

extern "C" {
#include <xcb/xcb.h>
#include <xcb/xproto.h>
}

#include "Compositor.hpp"
#include "managers/InputManager.hpp"
#include "managers/FullscreenController.hpp"
#include "desktop/view/Window.hpp"
#include "desktop/view/WindowState.hpp"
#include "desktop/state/FocusState.hpp"
#include "desktop/Workspace.hpp"
#include "state/WorkspaceState.hpp"
#include "output/MonitorState.hpp"
#include "layout/LayoutManager.hpp"
#include "layout/target/WindowTarget.hpp"
#include "layout/supplementary/WorkspaceAlgoMatcher.hpp"
#include "keybinds/Manager.hpp"
#include "keybinds/Bind.hpp"
#include "config/ConfigManager.hpp"

// -----------------------------------------------------------------------
// main.cpp — Точка входа: CCompositor::init() поднимает X11-соединение 
// + все системы по стадиям, дальше здесь только event loop, который 
// диспетчерит XCB-события в уже готовые менеджеры.
// -----------------------------------------------------------------------

static pid_t                 g_compositorProcPid = -1; // picom, не путать с CCompositor
static volatile sig_atomic_t g_running            = 1;
static std::unordered_map<xcb_window_t, unsigned> g_pendingWorkspaceUnmaps;

// ID workspace, с которого стартует основной монитор.
static constexpr int DEFAULT_WORKSPACE_ID = 1;

static void help() {
    std::println("usage: vexland [arg [...]]\n");
    std::println("Arguments:\n"
                  "    --help          -h   - Show this message\n"
                  "    --config FILE   -c   - Specify config file to use\n"
                  "    --no-compositor      - Do not spawn picom automatically\n"
                  "    --version       -v   - Print version");
}

static void onSigChld(int) {
    // пусто — реальный reap происходит в reapAnyUnattendedChildren()
}

static void reapAnyUnattendedChildren() {
    int status = 0;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        if (pid == g_compositorProcPid)
            continue;
    }
}

static void onTermSignal(int) {
    g_running = 0;
}

// -----------------------------------------------------------------------
// picom — "вшитый" композитор, спавнится как child-процесс
// -----------------------------------------------------------------------
static bool spawnCompositorProc(const std::string& configPath) {
    pid_t pid = fork();
    if (pid < 0) {
        std::println(stderr, "[ ERROR ] fork() failed while spawning compositor: {}", strerror(errno));
        return false;
    }

    if (pid == 0) {
        if (!configPath.empty())
            execlp("picom", "picom", "--config", configPath.c_str(), nullptr);
        else
            execlp("picom", "picom", nullptr);

        std::println(stderr, "[ ERROR ] failed to exec picom: {}", strerror(errno));
        _exit(1);
    }

    g_compositorProcPid = pid;
    std::println("[ INFO ] spawned compositor (picom) with pid {}", pid);
    return true;
}

static void superviseCompositorProc(const std::string& configPath) {
    if (g_compositorProcPid <= 0)
        return;

    int   status = 0;
    pid_t res    = waitpid(g_compositorProcPid, &status, WNOHANG);

    if (res == 0)
        return;

    if (res == g_compositorProcPid) {
        std::println(stderr, "[ WARN ] compositor (pid {}) exited unexpectedly, restarting...", g_compositorProcPid);
        g_compositorProcPid = -1;
        spawnCompositorProc(configPath);
    }
}

static void logX11Error(xcb_generic_error_t* err) {
    if (!err)
        return;
    std::println(stderr, "[ X11 ERROR ] code={} major={} minor={} resource={}", err->error_code, err->major_code, err->minor_code, err->resource_id);
    free(err);
}

static bool becomeWindowManager(xcb_connection_t* conn, xcb_window_t root) {
    uint32_t mask   = XCB_CW_EVENT_MASK;
    uint32_t values = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY;

    xcb_void_cookie_t    cookie = xcb_change_window_attributes_checked(conn, root, mask, &values);
    xcb_generic_error_t* err    = xcb_request_check(conn, cookie);

    if (err) {
        std::println(stderr, "[ ERROR ] another window manager is already running (SubstructureRedirect failed)");
        logX11Error(err);
        return false;
    }

    xcb_flush(conn);
    return true;
}

static PHLWORKSPACE createWorkspaceForMonitor(WORKSPACEID id, const PHLMONITOR& monitor) {
    if (!monitor)
        return nullptr;

    auto existing = CWorkspaceState::get()->byID(id);
    if (existing)
        return existing;

    auto ws = CWorkspaceState::get()->create(id, monitor->outputID());
    if (!ws) {
        std::println(stderr, "[ ERROR ] Failed to create workspace {}", id);
        return nullptr;
    }

    ws->m_space->setMonitorGeometry(monitor->x(), monitor->y(), monitor->w(), monitor->h());

    auto algo = CWorkspaceAlgoMatcher::get()->createAlgorithmForWorkspace(static_cast<int>(id));
    auto dwindleAlgo = std::dynamic_pointer_cast<CDwindleAlgorithm>(algo);
    if (!dwindleAlgo) {
        std::println(stderr, "[ ERROR ] Failed to create tiling algorithm for workspace {}", id);
        CWorkspaceState::get()->remove(id);
        return nullptr;
    }

    ws->m_space->setAlgorithm(dwindleAlgo);
    CLayoutManager::get()->registerSpace(static_cast<int>(id), ws->m_space);
    return ws;
}

static bool switchToWorkspace(xcb_connection_t* conn, const PHLMONITOR& monitor, WORKSPACEID id) {
    if (!conn || !monitor || id <= 0)
        return false;

    const WORKSPACEID currentID = monitor->activeWorkspaceID();
    if (currentID == id) {
        auto current = CWorkspaceState::get()->byID(id);
        if (current)
            current->m_visible = true;
        return true;
    }

    auto targetWorkspace = CWorkspaceState::get()->byID(id);
    if (!targetWorkspace)
        targetWorkspace = createWorkspaceForMonitor(id, monitor);
    if (!targetWorkspace)
        return false;

    auto currentWorkspace = CWorkspaceState::get()->byID(currentID);

    if (currentWorkspace)
        currentWorkspace->m_visible = false;

    targetWorkspace->m_monitorID = static_cast<int>(monitor->outputID());
    targetWorkspace->m_visible = true;

    for (const auto& w : CWindowState::get()->all()) {
        if (!w || w->m_workspaceID != currentID || !w->m_isMapped)
            continue;

        ++g_pendingWorkspaceUnmaps[w->getX11Window()];
        xcb_unmap_window(conn, w->getX11Window());
        w->m_isMapped = false;
    }

    for (const auto& w : CWindowState::get()->all()) {
        if (!w || w->m_workspaceID != id)
            continue;

        xcb_map_window(conn, w->getX11Window());
        w->m_isMapped = true;
    }

    monitor->setActiveWorkspaceID(static_cast<int>(id));
    CFocusState::get()->setMonitor(monitor);

    targetWorkspace->updateWindows();

    auto focusCandidate = targetWorkspace->getFocusCandidate();
    if (focusCandidate)
        CFocusState::get()->fullWindowFocus(conn, focusCandidate, FOCUS_REASON_KEYBIND);
    else
        CFocusState::get()->fullWindowFocus(conn, nullptr, FOCUS_REASON_KEYBIND);

    xcb_flush(conn);

    std::println("[ INFO ] switched workspace {} -> {} on monitor '{}'", currentID, id, monitor->name());
    return true;
}

static bool setupDefaultWorkspace() {
    auto monitor = CMonitorState::get()->primary();
    if (!monitor) {
        std::println(stderr, "[ ERROR ] No monitor available, cannot create default workspace");
        return false;
    }

    auto ws = createWorkspaceForMonitor(DEFAULT_WORKSPACE_ID, monitor);
    if (!ws)
        return false;

    ws->m_visible = true;
    monitor->setActiveWorkspaceID(DEFAULT_WORKSPACE_ID);
    CFocusState::get()->setMonitor(monitor);

    std::println("[ INFO ] default workspace {} created on monitor '{}' ({}x{})", DEFAULT_WORKSPACE_ID, monitor->name(), monitor->w(), monitor->h());
    return true;
}

// -----------------------------------------------------------------------
// Событийные хендлеры
// -----------------------------------------------------------------------

static void onMapRequest(xcb_connection_t* conn, xcb_map_request_event_t* ev) {
    auto w = CWindow::create(conn, ev->window);
    if (!w) {
        std::println(stderr, "[ WARN ] failed to create CWindow for xcb_window_t {}", ev->window);
        return;
    }

    CWindowState::get()->add(w);

    auto monitor = CFocusState::get()->monitor();
    if (!monitor)
        monitor = CMonitorState::get()->primary();

    const WORKSPACEID activeWorkspaceID = monitor ? monitor->activeWorkspaceID() : DEFAULT_WORKSPACE_ID;

    auto target = CWindowTarget::create(w);
    auto space  = CLayoutManager::get()->space(static_cast<int>(activeWorkspaceID));

    if (space) {
        CLayoutManager::get()->newTarget(target, space);
    } else {
        std::println(stderr, "[ WARN ] no default workspace Space found, window {} will not be tiled", ev->window);
    }

    w->m_workspaceID = static_cast<int>(activeWorkspaceID);
    w->m_monitorID   = monitor ? static_cast<int>(monitor->outputID()) : -1;
    w->m_isMapped    = true;

    // Делегируем установку масок ввода InputManager'у
    CInputManager::get()->onWindowCreated(ev->window);

    xcb_map_window(conn, ev->window);
    xcb_flush(conn);

    CFocusState::get()->fullWindowFocus(conn, w, FOCUS_REASON_NEW_WINDOW);

    std::println("[ INFO ] mapped window {} (stableID {})", ev->window, w->stableID());
}

static void onDestroyNotify(xcb_connection_t* conn, xcb_destroy_notify_event_t* ev) {
    auto w = CWindowState::get()->byXWindow(ev->window);
    if (!w)
        return;

    if (CFocusState::get()->window() == w)
        CFocusState::get()->fullWindowFocus(conn, nullptr);

    g_pendingWorkspaceUnmaps.erase(ev->window);
    CWindowState::get()->remove(w->stableID());
    std::println("[ INFO ] destroyed window {} (stableID {})", ev->window, w->stableID());
}

static void onUnmapNotify(xcb_unmap_notify_event_t* ev) {
    auto w = CWindowState::get()->byXWindow(ev->window);
    if (!w)
        return;

    auto pendingIt = g_pendingWorkspaceUnmaps.find(ev->window);
    if (pendingIt != g_pendingWorkspaceUnmaps.end()) {
        if (--pendingIt->second == 0)
            g_pendingWorkspaceUnmaps.erase(pendingIt);

        auto monitor = CFocusState::get()->monitor();
        if (monitor && monitor->activeWorkspaceID() == w->m_workspaceID) {
            w->m_isMapped = true;
        } else {
            w->m_isMapped = false;
        }
        return;
    }

    w->m_isMapped = false;
    std::println("[ INFO ] unmapped window {} (stableID {})", ev->window, w->stableID());
}

static Input::ModifierMask xcbStateToModMask(uint16_t state) {
    Input::ModifierMask mods = Input::HL_MODIFIER_NONE;

    if (state & XCB_MOD_MASK_SHIFT)
        mods |= Input::HL_MODIFIER_SHIFT;
    if (state & XCB_MOD_MASK_CONTROL)
        mods |= Input::HL_MODIFIER_CTRL;
    if (state & XCB_MOD_MASK_1)
        mods |= Input::HL_MODIFIER_ALT;
    if (state & XCB_MOD_MASK_4)
        mods |= Input::HL_MODIFIER_META;

    return mods;
}

static void onKeyPress(xcb_key_press_event_t* ev) {
    Input::ModifierMask mods = xcbStateToModMask(ev->state);
    CKeybindManager::get()->onKeyEvent(ev->detail, mods, /*pressed=*/true);
}

static void onKeyRelease(xcb_key_release_event_t* ev) {
    Input::ModifierMask mods = xcbStateToModMask(ev->state);
    CKeybindManager::get()->onKeyEvent(ev->detail, mods, /*pressed=*/false);
}

static void spawnApp(const std::string& cmd) {
    pid_t pid = fork();
    if (pid < 0) {
        std::println(stderr, "[ ERROR ] fork() failed while spawning '{}': {}", cmd, strerror(errno));
        return;
    }

    if (pid == 0) {
        execlp("/bin/sh", "sh", "-c", cmd.c_str(), nullptr);
        std::println(stderr, "[ ERROR ] failed to exec '{}': {}", cmd, strerror(errno));
        _exit(1);
    }
}

static Input::ModifierMask parseModString(const std::string& modStr) {
    Input::ModifierMask mask = Input::HL_MODIFIER_NONE;

    std::string upper = modStr;
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return std::toupper(c); });

    size_t start = 0;
    while (start <= upper.size()) {
        size_t      plusPos = upper.find('+', start);
        std::string part    = upper.substr(start, plusPos == std::string::npos ? std::string::npos : plusPos - start);

        if (part == "CTRL" || part == "CONTROL")
            mask |= Input::HL_MODIFIER_CTRL;
        else if (part == "SHIFT")
            mask |= Input::HL_MODIFIER_SHIFT;
        else if (part == "ALT")
            mask |= Input::HL_MODIFIER_ALT;
        else if (part == "SUPER" || part == "META" || part == "MOD4")
            mask |= Input::HL_MODIFIER_META;
        else if (!part.empty())
            std::println(stderr, "[ WARN ] unknown modifier '{}' in keybind config", part);

        if (plusPos == std::string::npos)
            break;
        start = plusPos + 1;
    }

    return mask;
}

static void registerConfigKeybinds() {
    for (const auto& kb : CConfigManager::get()->keybinds()) {
        Input::ModifierMask mods = parseModString(kb.mod);

        auto bind = CBind::make(mods, kb.key, 0, [action = kb.action, args = kb.args]() -> SBindResult {
            if (action == "spawn" && !args.empty()) {
                spawnApp(args[0]);
                return {.success = true};
            }

            if (action == "workspace" && !args.empty()) {
                try {
                    const auto id = static_cast<WORKSPACEID>(std::stoll(args[0]));
                    auto monitor = CFocusState::get()->monitor();
                    if (!monitor)
                        monitor = CMonitorState::get()->primary();

                    if (!monitor || !switchToWorkspace(g_pCompositor->m_conn, monitor, id)) {
                        std::println(stderr, "[ WARN ] failed to switch to workspace {}", id);
                        return {.success = false};
                    }

                    return {.success = true};
                } catch (const std::exception&) {
                    std::println(stderr, "[ WARN ] invalid workspace id '{}' in keybind", args[0]);
                    return {.success = false};
                }
            }

            if (action == "killactive") {
                auto w = CFocusState::get()->window();
                if (w) {
                    xcb_destroy_window(g_pCompositor->m_conn, w->getX11Window());
                }
                return {.success = true};
            }

            if (action == "fullscreen") {
                auto w = CFocusState::get()->window();
                if (w) {
                    CFullscreenController::get()->setFullscreenMode(g_pCompositor->m_conn, w, !w->m_isFullscreen);
                }
                return {.success = true};
            }

            std::println("[ INFO ] keybind fired: action='{}' (dispatcher for this action not yet implemented)", action);
            return {.success = true};
        });

        if (bind) {
            CKeybindManager::get()->addBind(std::move(*bind));
            std::println("[ INFO ] registered keybind: mod='{}' key='{}' action='{}'", kb.mod, kb.key, kb.action);
        } else {
            std::println(stderr, "[ WARN ] failed to register keybind from config: mod='{}' key='{}'", kb.mod, kb.key);
        }
    }
}

static void registerHardcodedTestBind() {
    auto bindKitty = CBind::make(Input::HL_MODIFIER_CTRL, "q", 0, []() -> SBindResult {
        std::println("[ INFO ] Ctrl+Q pressed — spawning kitty (fallback bind)");
        spawnApp("kitty");
        return {.success = true};
    });

    if (bindKitty)
        CKeybindManager::get()->addBind(std::move(*bindKitty));
    else
        std::println(stderr, "[ WARN ] failed to register fallback keybind Ctrl+Q");
}

static void eventLoop(xcb_connection_t* conn, const std::string& picomConfigPath) {
    while (g_running) {
        superviseCompositorProc(picomConfigPath);
        reapAnyUnattendedChildren();

        xcb_generic_event_t* event = xcb_poll_for_event(conn);
        if (!event) {
            usleep(1000); 
            continue;
        }

        switch (event->response_type & ~0x80) {
            case XCB_MAP_REQUEST: onMapRequest(conn, reinterpret_cast<xcb_map_request_event_t*>(event)); break;
            case XCB_DESTROY_NOTIFY: onDestroyNotify(conn, reinterpret_cast<xcb_destroy_notify_event_t*>(event)); break;
            case XCB_UNMAP_NOTIFY: onUnmapNotify(reinterpret_cast<xcb_unmap_notify_event_t*>(event)); break;
            case XCB_KEY_PRESS: onKeyPress(reinterpret_cast<xcb_key_press_event_t*>(event)); break;
            case XCB_KEY_RELEASE: onKeyRelease(reinterpret_cast<xcb_key_release_event_t*>(event)); break;
            
            // Делегируем события мыши в InputManager
            case XCB_ENTER_NOTIFY: 
            case XCB_BUTTON_PRESS:
                CInputManager::get()->onMouseEvent(event); 
                break;

            case XCB_MAPPING_NOTIFY:
                CKeybindManager::get()->updateXkbState(conn);
                std::println("[ INFO ] keyboard mapping changed, xkb state reloaded");
                break;
            case 0: {
                logX11Error(reinterpret_cast<xcb_generic_error_t*>(event));
                free(event);
                continue;
            }
            default: break;
        }

        free(event);
    }
}

int main(int argc, char** argv) {
    std::string configPath;
    bool        spawnComp = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            help();
            return 0;
        } else if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            configPath = argv[++i];
        } else if (arg == "--no-compositor") {
            spawnComp = false;
        } else if (arg == "-v" || arg == "--version") {
            std::println("vexland 0.0.1-dev");
            return 0;
        }
    }

    signal(SIGCHLD, onSigChld);
    signal(SIGTERM, onTermSignal);
    signal(SIGINT, onTermSignal);

    g_pCompositor = std::make_unique<CCompositor>(/*onlyConfigVerification=*/false);

    if (!g_pCompositor->init(configPath)) {
        std::println(stderr, "[ ERROR ] Compositor initialization failed");
        return 1;
    }

    if (!becomeWindowManager(g_pCompositor->m_conn, g_pCompositor->m_root))
        return 1;

    // Инициализируем менеджеров
    CKeybindManager::get()->setX11Connection(g_pCompositor->m_conn, g_pCompositor->m_root);
    CInputManager::get()->init(g_pCompositor->m_conn, g_pCompositor->m_root);

    if (!setupDefaultWorkspace())
        return 1;

    registerConfigKeybinds();
    registerHardcodedTestBind();
    CKeybindManager::get()->regrabKeys();

    if (spawnComp)
        spawnCompositorProc("");

    g_pCompositor->startCompositor();

    std::println("[ INFO ] Vexland started, entering event loop");
    eventLoop(g_pCompositor->m_conn, "");

    std::println("[ INFO ] shutting down");
    g_pCompositor->cleanup();

    if (g_compositorProcPid > 0) {
        kill(g_compositorProcPid, SIGTERM);
        waitpid(g_compositorProcPid, nullptr, 0);
    }

    return 0;
}
