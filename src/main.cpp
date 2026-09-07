#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <string>
#include <print>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <filesystem>
#include <fstream>

#include <unistd.h>
#include <sys/wait.h>

extern "C" {
#include <xcb/xcb.h>
#include <xcb/xproto.h>
}

#include "Compositor.hpp"
#include "managers/InputManager.hpp"
#include "managers/FullscreenController.hpp"
#include "managers/AnimationManager.hpp"
#include "managers/VXR.hpp"
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
// resolvePicomConfigPath — ищет picom.conf по стандартным XDG-путям,
// если пользователь явно не передал --picom-config. Не обязателен:
// если нигде не найден, picom запускается со своими дефолтами (как и
// раньше — пустая строка означает "без --config").
//
// Порядок поиска (первое совпадение побеждает):
//   1. $XDG_CONFIG_HOME/vexland/picom.conf
//   2. ~/.config/vexland/picom.conf
// -----------------------------------------------------------------------
static std::string resolvePicomConfigPath() {
    namespace fs = std::filesystem;

    if (const char* xdgConfig = std::getenv("XDG_CONFIG_HOME"); xdgConfig && *xdgConfig) {
        fs::path candidate = fs::path(xdgConfig) / "vexland" / "picom.conf";
        if (fs::exists(candidate))
            return candidate.string();
    }

    if (const char* home = std::getenv("HOME"); home && *home) {
        fs::path candidate = fs::path(home) / ".config" / "vexland" / "picom.conf";
        if (fs::exists(candidate))
            return candidate.string();
    }

    return {};
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

    // КРИТИЧНО: CSpace хранит только weak_ptr на target'ы (см.
    // Window.hpp::windowTarget() комментарий) — без этой строки
    // shared_ptr в `target` умирал бы вместе с концом этой функции,
    // и все weak_ptr на него (в т.ч. в дереве dwindle-алгоритма)
    // истекали бы сразу после первого recalculate. Раньше это
    // проявлялось как "второе открытое окно не тайлится с первым —
    // первое остаётся во весь экран".
    w->setWindowTarget(target);

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

    // Redirect окна в VXR для потенциальной прозрачности (no-op если
    // VXR::isAvailable() == false — не требует отдельной проверки
    // здесь, registerWindow сама проверяет).
    VXR::registerWindow(conn, w);

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

    // Убираем target из layout-дерева ДО удаления окна из реестра —
    // иначе dwindle-алгоритм продолжит делить пространство так, будто
    // окно всё ещё существует (лист останется в дереве, просто со
    // "зависшим" target'ом). Найдено при отладке того же класса бага,
    // что и владение shared_ptr<ITarget> — см. комментарий в
    // Window.hpp::windowTarget().
    if (auto target = w->windowTarget())
        CLayoutManager::get()->removeTarget(target);

    // Освобождаем X11-ресурсы VXR ДО удаления окна из реестра — после
    // remove() shared_ptr может уничтожиться, и getX11Window() внутри
    // unregisterWindow больше не будет валиден для построения запросов.
    VXR::unregisterWindow(conn, w);

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

// -----------------------------------------------------------------------
// closeWindowGracefully — аналог "close window" из toolkit-совместимых
// WM: сначала пробуем ICCCM WM_DELETE_WINDOW (даём клиенту шанс спросить
// "сохранить перед выходом?" и т.д.), и только если клиент не объявил
// поддержку этого протокола в WM_PROTOCOLS — жёстко убиваем через
// xcb_destroy_window как раньше.
//
// Протокол: https://tronche.com/gui/x/icccm/sec-4.html#s-4.2.8.1
// -----------------------------------------------------------------------
static bool windowSupportsProtocol(xcb_connection_t* conn, xcb_window_t win, xcb_atom_t wmProtocols, xcb_atom_t wmDeleteWindow) {
    xcb_get_property_cookie_t cookie = xcb_get_property(conn, 0, win, wmProtocols, XCB_ATOM_ATOM, 0, 32);
    xcb_get_property_reply_t* reply  = xcb_get_property_reply(conn, cookie, nullptr);

    if (!reply)
        return false;

    bool supports = false;

    if (reply->type == XCB_ATOM_ATOM && reply->format == 32) {
        const xcb_atom_t* atoms  = static_cast<xcb_atom_t*>(xcb_get_property_value(reply));
        const int          count = xcb_get_property_value_length(reply) / sizeof(xcb_atom_t);

        for (int i = 0; i < count; ++i) {
            if (atoms[i] == wmDeleteWindow) {
                supports = true;
                break;
            }
        }
    }

    free(reply);
    return supports;
}

static void closeWindowGracefully(xcb_connection_t* conn, xcb_window_t win) {
    if (!conn || !win)
        return;

    static xcb_atom_t wmProtocols   = XCB_ATOM_NONE;
    static xcb_atom_t wmDeleteWindow = XCB_ATOM_NONE;

    if (wmProtocols == XCB_ATOM_NONE) {
        xcb_intern_atom_cookie_t c1 = xcb_intern_atom(conn, 1, strlen("WM_PROTOCOLS"), "WM_PROTOCOLS");
        xcb_intern_atom_reply_t* r1 = xcb_intern_atom_reply(conn, c1, nullptr);
        if (r1) {
            wmProtocols = r1->atom;
            free(r1);
        }

        xcb_intern_atom_cookie_t c2 = xcb_intern_atom(conn, 1, strlen("WM_DELETE_WINDOW"), "WM_DELETE_WINDOW");
        xcb_intern_atom_reply_t* r2 = xcb_intern_atom_reply(conn, c2, nullptr);
        if (r2) {
            wmDeleteWindow = r2->atom;
            free(r2);
        }
    }

    if (wmProtocols == XCB_ATOM_NONE || wmDeleteWindow == XCB_ATOM_NONE ||
        !windowSupportsProtocol(conn, win, wmProtocols, wmDeleteWindow)) {
        // клиент не поддерживает вежливое закрытие (или атомы не резолвились) —
        // fallback на то, что было раньше
        std::println("[ INFO ] window {} doesn't support WM_DELETE_WINDOW, destroying directly", win);
        xcb_destroy_window(conn, win);
        return;
    }

    xcb_client_message_event_t ev = {};
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format        = 32;
    ev.window         = win;
    ev.type           = wmProtocols;
    ev.data.data32[0] = wmDeleteWindow;
    ev.data.data32[1] = XCB_CURRENT_TIME;

    xcb_send_event(conn, 0, win, XCB_EVENT_MASK_NO_EVENT, reinterpret_cast<const char*>(&ev));
    xcb_flush(conn);

    std::println("[ INFO ] sent WM_DELETE_WINDOW to window {}", win);
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
                    closeWindowGracefully(g_pCompositor->m_conn, w->getX11Window());
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

        // Анимации не привязаны к X11-событиям — тикаем на каждой
        // итерации busy-loop'а, tick() сам решает (через внутренний
        // throttle), пора ли применить следующий кадр. Дёшево звать
        // часто: early-return, если рано или анимировать нечего.
        AnimationManager::tick();

        // VXR compositing pass — ПОСЛЕ AnimationManager::tick(), чтобы
        // рисовать уже актуальную (анимированную) geometry, а не кадр
        // на шаг позади. No-op внутри, если VXR недоступен или все
        // окна непрозрачны — дёшево звать на каждой итерации.
        VXR::repaint(conn, g_pCompositor->m_root);

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

// -----------------------------------------------------------------------
// resolveOrCreateConfigPath — если пользователь не передал -c/--config,
// ищем стандартный путь ~/.config/vexland/vexland.lua. Если его тоже
// нет — создаём директорию и записываем встроенный дефолтный конфиг
// (см. DEFAULT_CONFIG_LUA ниже), чтобы у пользователя сразу было с
// чем работать, а не пустой WM без единого бинда.
//
// Шаблон встроен как строковая константа в бинарник (а не читается из
// default_config/vexland.lua на диске), потому что бинарник может
// запускаться не из корня репозитория — установочный путь к исходникам
// не гарантирован.
// -----------------------------------------------------------------------

static const char* DEFAULT_CONFIG_LUA = R"LUACONF(-- -----------------------------------------------------------------------
-- vexland.lua — автосгенерированный конфиг. Отредактируй под себя —
-- при следующем запуске Vexland НЕ перезапишет этот файл повторно.
-- -----------------------------------------------------------------------

local terminal = "xterm"
local menu     = "rofi -show drun"

-- Обои — путь ниже нужно поменять на свой файл. VXR (наш встроенный
-- compositor) читает реальный root pixmap через _XROOTPMAP_ID, который
-- feh/nitrogen/xwallpaper выставляют сами — без этого прозрачные окна
-- будут показывать сплошной чёрный фон вместо обоев.
vx.exec_once("feh --bg-fill ~/wallpaper.jpg")

-- vx.exec_once("polybar")

vx.set("general.gaps_in", 5)
vx.set("general.gaps_out", 10)

vx.set("general.border_size", 2)
vx.set("general.col.active_border", "88c0d0")
vx.set("general.col.inactive_border", "3b4252")

vx.set("decoration.rounding", 8)

-- Прозрачность окон через VXR (наш собственный XRender compositor,
-- без picom). 1.0 = непрозрачно. active_opacity влияет и на активное
-- (сфокусированное) окно тоже — раскомментируй, если хочешь чтобы
-- ВСЕ окна были полупрозрачными, а не только неактивные.
vx.set("general.inactive_opacity", 0.85)
-- vx.set("general.active_opacity", 0.95)

vx.bind("CTRL", "RETURN", "spawn", terminal)
vx.bind("CTRL", "D", "spawn", menu)

vx.bind("CTRL", "H", "killactive")
vx.bind("CTRL", "F", "fullscreen")

vx.bind("CTRL", "1", "workspace", "1")
vx.bind("CTRL", "2", "workspace", "2")
vx.bind("CTRL", "3", "workspace", "3")
vx.bind("CTRL", "4", "workspace", "4")
vx.bind("CTRL", "5", "workspace", "5")
)LUACONF";

static std::string resolveOrCreateConfigPath() {
    namespace fs = std::filesystem;

    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        std::println(stderr, "[ WARN ] $HOME not set, cannot resolve default config path");
        return {};
    }

    fs::path configDir  = fs::path(home) / ".config" / "vexland";
    fs::path configFile = configDir / "vexland.lua";

    if (fs::exists(configFile))
        return configFile.string();

    std::error_code ec;
    fs::create_directories(configDir, ec);
    if (ec) {
        std::println(stderr, "[ WARN ] failed to create {}: {}", configDir.string(), ec.message());
        return {};
    }

    std::ofstream out(configFile);
    if (!out) {
        std::println(stderr, "[ WARN ] failed to create default config at {}", configFile.string());
        return {};
    }

    out << DEFAULT_CONFIG_LUA;
    out.close();

    std::println("[ INFO ] no config found, generated default at {}", configFile.string());
    return configFile.string();
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

    // Если -c/--config не передан — ищем/создаём дефолтный конфиг по
    // стандартному XDG-подобному пути, а не остаёмся вообще без конфига.
    if (configPath.empty())
        configPath = resolveOrCreateConfigPath();

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

    // exec_once — запускаем ПОСЛЕ регистрации биндов, но ДО event loop,
    // чтобы автозапущенные программы (панель, обои, дефолтный терминал)
    // появлялись сразу с рабочими хоткеями, а не в окне гонки, когда
    // WM ещё не готов их обрабатывать.
    for (const auto& cmd : CConfigManager::get()->execOnceCommands())
        spawnApp(cmd);

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
