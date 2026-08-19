#include <cstdio>
#include <cstring>
#include <csignal>
#include <string>
#include <print>

#include <unistd.h>
#include <sys/wait.h>

extern "C" {
#include <xcb/xcb.h>
#include <xcb/xproto.h>
}

#include "Compositor.hpp"
#include "desktop/view/Window.hpp"
#include "desktop/view/WindowState.hpp"
#include "desktop/state/FocusState.hpp"
#include "desktop/Workspace.hpp"
#include "state/WorkspaceState.hpp"
#include "output/MonitorState.hpp"
#include "layout/LayoutManager.hpp"
#include "layout/target/WindowTarget.hpp"
#include "keybinds/Manager.hpp"
#include "keybinds/Bind.hpp"
#include "config/ConfigManager.hpp"

// -----------------------------------------------------------------------
// main.cpp — финальная версия. Точка входа: CCompositor::init() поднимает
// X11-соединение + все системы по стадиям, дальше здесь только event
// loop, который диспетчерит XCB-события в уже готовые менеджеры.
//
// Ничего не дублирует логику Compositor.cpp — main.cpp тонкий, вся
// содержательная инициализация внутри CCompositor.
// -----------------------------------------------------------------------

static pid_t                 g_compositorProcPid = -1; // picom, не путать с CCompositor
static volatile sig_atomic_t g_running            = 1;

// ID единственного workspace на старте — пока нет полноценного
// multi-workspace switching UI (keybind "перейти на workspace N" и
// связанный monitor-tracking), все новые окна падают в этот workspace.
// TODO: когда появится workspace-switching keybind, это должно стать
// динамическим "текущий активный workspace текущего монитора".
static constexpr int DEFAULT_WORKSPACE_ID = 1;

static void help() {
    std::println("usage: vexland [arg [...]]\n");
    std::println("Arguments:\n"
                  "    --help          -h   - Show this message\n"
                  "    --config FILE   -c   - Specify config file to use\n"
                  "    --no-compositor      - Do not spawn picom automatically\n"
                  "    --version       -v   - Print version");
}

static void reapZombieChildrenAutomatically() {
    struct sigaction act{};
    act.sa_handler = SIG_DFL;
    sigemptyset(&act.sa_mask);
    act.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &act, nullptr);
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

// Регистрируем WM через SubstructureRedirect — если другой WM уже
// занял дисплей, тут провалимся с понятной ошибкой, не крашемся.
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

// Создаёт дефолтный workspace на основном мониторе и регистрирует его
// Space в LayoutManager — без этого CLayoutManager::newTarget() не
// сможет найти, куда класть новые окна.
static bool setupDefaultWorkspace() {
    auto monitor = CMonitorState::get()->primary();
    if (!monitor) {
        std::println(stderr, "[ ERROR ] No monitor available, cannot create default workspace");
        return false;
    }

    auto ws = CWorkspaceState::get()->create(DEFAULT_WORKSPACE_ID, monitor->outputID());
    if (!ws) {
        std::println(stderr, "[ ERROR ] Failed to create default workspace");
        return false;
    }

    // геометрия монитора реальная (из XRandR), не заглушка — правим
    // Space, который CWorkspace::create() построил с временными
    // значениями 1920x1080 (см. TODO в Workspace.cpp)
    ws->m_space->setMonitorGeometry(monitor->x(), monitor->y(), monitor->w(), monitor->h());

    CLayoutManager::get()->registerSpace(DEFAULT_WORKSPACE_ID, ws->m_space);

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

    // оборачиваем окно в WindowTarget и кладём в layout дефолтного
    // workspace — вот тут окно реально начинает участвовать в тайлинге
    auto target = CWindowTarget::create(w);
    auto space  = CLayoutManager::get()->space(DEFAULT_WORKSPACE_ID);

    if (space) {
        CLayoutManager::get()->newTarget(target, space);
    } else {
        std::println(stderr, "[ WARN ] no default workspace Space found, window {} will not be tiled", ev->window);
    }

    xcb_map_window(conn, ev->window);
    xcb_flush(conn);

    // сразу фокусируем новое окно — стандартное поведение большинства WM
    CFocusState::get()->fullWindowFocus(conn, w, FOCUS_REASON_NEW_WINDOW);

    std::println("[ INFO ] mapped window {} (stableID {})", ev->window, w->stableID());
}

static void onDestroyNotify(xcb_connection_t* conn, xcb_destroy_notify_event_t* ev) {
    auto w = CWindowState::get()->byXWindow(ev->window);
    if (!w)
        return;

    // если уничтоженное окно было сфокусировано — снимаем фокус (уходит
    // на root window), иначе X-сервер продолжит слать ввод в никуда
    if (CFocusState::get()->window() == w)
        CFocusState::get()->fullWindowFocus(conn, nullptr);

    CWindowState::get()->remove(w->stableID());
    std::println("[ INFO ] destroyed window {} (stableID {})", ev->window, w->stableID());
}

static void onUnmapNotify(xcb_unmap_notify_event_t* ev) {
    auto w = CWindowState::get()->byXWindow(ev->window);
    if (!w)
        return;

    w->m_isMapped = false;
    std::println("[ INFO ] unmapped window {} (stableID {})", ev->window, w->stableID());
}

// Конвертирует X11 event->state (модификаторы из XCB, битовая маска
// XCB_MOD_MASK_*) в наш Input::ModifierMask. Отдельная функция, потому
// что XCB и наш eKeyboardModifiers — разные битовые схемы, смешивать
// их напрямую было бы тихой засадой на будущее.
static Input::ModifierMask xcbStateToModMask(uint16_t state) {
    Input::ModifierMask mods = Input::HL_MODIFIER_NONE;

    if (state & XCB_MOD_MASK_SHIFT)
        mods |= Input::HL_MODIFIER_SHIFT;
    if (state & XCB_MOD_MASK_CONTROL)
        mods |= Input::HL_MODIFIER_CTRL;
    if (state & XCB_MOD_MASK_1) // Mod1 = обычно Alt
        mods |= Input::HL_MODIFIER_ALT;
    if (state & XCB_MOD_MASK_4) // Mod4 = обычно Super/Windows key
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
// Спавнит произвольную программу через fork+execlp — та же схема, что
// и spawnCompositorProc для picom, но для обычных приложений (kitty
// и т.д.), запускаемых из keybind'ов.
// -----------------------------------------------------------------------
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

    // не ждём (waitpid) — зомби подчищает reapZombieChildrenAutomatically()
}

// -----------------------------------------------------------------------
// Регистрирует тестовые keybind'ы напрямую в коде — временно, пока нет
// полноценной конвертации ConfigManager::keybinds() (SKeybind структуры
// из vx.bind() в Lua) в реальные CBind объекты.
//
// TODO: заменить на реальный цикл по CConfigManager::get()->keybinds(),
// конвертирующий каждый SKeybind в CBind через CBind::make(...) и
// регистрирующий его в CKeybindManager::get()->addBind(...).
// -----------------------------------------------------------------------
static void registerHardcodedTestBind(xcb_connection_t* conn) {
    // Ctrl+Q -> открыть kitty (тестовый сценарий: проверить что
    // keybind pipeline реально работает end-to-end)
    auto bindKitty = CBind::make(Input::HL_MODIFIER_CTRL, "q", 0, []() -> SBindResult {
        std::println("[ INFO ] Ctrl+Q pressed — spawning kitty");
        spawnApp("kitty");
        return {.success = true};
    });

    if (bindKitty)
        CKeybindManager::get()->addBind(std::move(*bindKitty));
    else
        std::println(stderr, "[ WARN ] failed to register test keybind Ctrl+Q");
}

static void eventLoop(xcb_connection_t* conn, const std::string& picomConfigPath) {
    while (g_running) {
        superviseCompositorProc(picomConfigPath);

        xcb_generic_event_t* event = xcb_poll_for_event(conn);
        if (!event) {
            usleep(1000); // TODO: заменить на epoll/select на xcb_get_file_descriptor(conn) для честного блокирования
            continue;
        }

        switch (event->response_type & ~0x80) {
            case XCB_MAP_REQUEST: onMapRequest(conn, reinterpret_cast<xcb_map_request_event_t*>(event)); break;
            case XCB_DESTROY_NOTIFY: onDestroyNotify(conn, reinterpret_cast<xcb_destroy_notify_event_t*>(event)); break;
            case XCB_UNMAP_NOTIFY: onUnmapNotify(reinterpret_cast<xcb_unmap_notify_event_t*>(event)); break;
            case XCB_KEY_PRESS: onKeyPress(reinterpret_cast<xcb_key_press_event_t*>(event)); break;
            case XCB_KEY_RELEASE: onKeyRelease(reinterpret_cast<xcb_key_release_event_t*>(event)); break;
            case XCB_MAPPING_NOTIFY:
                // раскладка сменилась (setxkbmap и т.д.) — пересобираем xkb state
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

    reapZombieChildrenAutomatically();
    signal(SIGTERM, onTermSignal);
    signal(SIGINT, onTermSignal);

    // CCompositor::init() поднимает X11 connection + все системы по
    // стадиям (ConfigManager -> MonitorState/WindowState/WorkspaceState
    // -> FocusState/LayoutManager/KeybindManager). Вся содержательная
    // инициализация внутри Compositor.cpp, main.cpp её не дублирует.
    g_pCompositor = std::make_unique<CCompositor>(/*onlyConfigVerification=*/false);

    if (!g_pCompositor->init(configPath)) {
        std::println(stderr, "[ ERROR ] Compositor initialization failed");
        return 1;
    }

    if (!becomeWindowManager(g_pCompositor->m_conn, g_pCompositor->m_root))
        return 1;

    if (!setupDefaultWorkspace())
        return 1;

    // временный тестовый бинд, пока нет полной ConfigManager -> CBind
    // конвертации (см. TODO у registerHardcodedTestBind)
    registerHardcodedTestBind(g_pCompositor->m_conn);

    if (spawnComp)
        spawnCompositorProc(""); // TODO: путь до дефолтного picom.conf

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
