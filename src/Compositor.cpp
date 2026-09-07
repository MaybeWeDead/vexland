#include "Compositor.hpp"

#include "desktop/view/WindowState.hpp"
#include "state/WorkspaceState.hpp"
#include "output/MonitorState.hpp"
#include "desktop/state/FocusState.hpp"
#include "layout/LayoutManager.hpp"
#include "config/ConfigManager.hpp"
#include "keybinds/Manager.hpp"
#include "managers/WindowShape.hpp"
#include "managers/VXR.hpp"
#include "managers/Splash.hpp"

#include <cstdio>
#include <print>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#if defined(__linux__)
#include <linux/vt.h>
#endif

// -----------------------------------------------------------------------
// Compositor.cpp — initManagers(stage) переносит трёхфазный паттерн
// оригинала на наши уже написанные системы. getVTNr()/bumpNofile()/
// restoreNofile() перенесены почти дословно — эта часть вообще не
// завязана на Wayland/Aquamarine, чистый POSIX.
// -----------------------------------------------------------------------

CCompositor::CCompositor(bool onlyConfigVerification) : m_onlyConfigVerification(onlyConfigVerification) {}

CCompositor::~CCompositor() {
    if (!m_isShuttingDown && !m_onlyConfigVerification)
        cleanup();
}

void CCompositor::bumpNofile() {
    if (getrlimit(RLIMIT_NOFILE, &m_originalNofile) != 0) {
        std::println(stderr, "[ ERROR ] Failed to get NOFILE rlimits");
        m_originalNofile.rlim_max = 0;
        return;
    }

    rlimit newLimit = m_originalNofile;
    newLimit.rlim_cur = newLimit.rlim_max;

    if (setrlimit(RLIMIT_NOFILE, &newLimit) < 0) {
        std::println(stderr, "[ ERROR ] Failed bumping NOFILE limits higher");
        m_originalNofile.rlim_max = 0;
        return;
    }

    std::println("[ INFO ] NOFILE limit bumped to {}", newLimit.rlim_cur);
}

void CCompositor::restoreNofile() {
    if (m_originalNofile.rlim_max <= 0)
        return;

    if (setrlimit(RLIMIT_NOFILE, &m_originalNofile) < 0)
        std::println(stderr, "[ ERROR ] Failed restoring NOFILE limits");
}

// Аналог getVTNr() — перенесено почти дословно, это чистый POSIX/Linux
// ioctl, не завязан на Wayland/Aquamarine session вообще. Полезно для
// диагностики ("на каком VT мы сейчас работаем") и для handleVT логики
// в keybinds/Manager.cpp (проверка "уже на этом VT — не переключаемся").
std::optional<unsigned int> CCompositor::getVTNr() const {
    unsigned int ttynum = 0;

    int fd = open("/dev/tty", O_RDONLY | O_NOCTTY);
    if (fd < 0)
        return std::nullopt;

#if defined(VT_GETSTATE)
    struct vt_stat st;
    if (!ioctl(fd, VT_GETSTATE, &st))
        ttynum = st.v_active;
#elif defined(VT_GETACTIVE)
    int vt;
    if (!ioctl(fd, VT_GETACTIVE, &vt))
        ttynum = vt;
#endif

    close(fd);

    if (ttynum == 0)
        return std::nullopt;

    return ttynum;
}

// Аналог initManagers(stage) — тот же трёхфазный паттерн, под наши
// системы. Порядок важен: ConfigManager должен грузиться ДО остальных
// реестров (значения типа gaps нужны сразу при первом recalculate),
// MonitorState — до WorkspaceState (workspace должен знать реальную
// геометрию монитора при создании, не заглушку).
void CCompositor::initManagers(eManagersInitStage stage) {
    switch (stage) {
        case STAGE_PRIORITY: {
            std::println("[ INFO ] Creating the ConfigManager");
            // ConfigManager — синглтон через get(), первый вызов создаёт
            // экземпляр. load() вызывается отдельно из init() ниже —
            // тут только гарантируем что объект существует.
            CConfigManager::get();
        } break;

        case STAGE_BASICINIT: {
            std::println("[ INFO ] Creating MonitorState");
            if (!CMonitorState::get()->refresh(m_conn, m_root))
                std::println(stderr, "[ WARN ] No monitors found during initial XRandR refresh");

            std::println("[ INFO ] Creating WindowState");
            CWindowState::get();

            std::println("[ INFO ] Creating WorkspaceState");
            CWorkspaceState::get();
        } break;

        case STAGE_LATE: {
            std::println("[ INFO ] Creating FocusState");
            CFocusState::get();

            std::println("[ INFO ] Creating LayoutManager");
            CLayoutManager::get();

            std::println("[ INFO ] Creating KeybindManager, initializing xkbcommon-x11");
            if (!CKeybindManager::get()->initXkb(m_conn))
                std::println(stderr, "[ ERROR ] Failed to initialize xkbcommon-x11 — keyboard input will not work");
        } break;

        default: break;
    }
}

// Аналог initServer, но под X11 подключение вместо Wayland socket
// handover. Возвращает false при критичной ошибке (X11 connect fail),
// но НЕ падает исключением — main.cpp сам решает, выходить или нет.
bool CCompositor::init(const std::string& configPath) {
    bumpNofile();

    m_conn = xcb_connect(nullptr, nullptr);
    if (!m_conn || xcb_connection_has_error(m_conn)) {
        std::println(stderr, "[ ERROR ] Failed to connect to X server");
        return false;
    }

    const xcb_setup_t*    setup = xcb_get_setup(m_conn);
    xcb_screen_iterator_t iter  = xcb_setup_roots_iterator(setup);
    m_root                       = iter.data->root;

    // Shape extension — нужна для скруглённых углов (см. WindowShape.cpp).
    // Проверяем один раз тут, а не при каждом applyRounding() —
    // xcb_get_extension_data кэширует внутри XCB, но нет смысла дёргать
    // это на каждый resize окна.
    WindowShape::queryShapeExtension(m_conn);

    // VXR — минимальный XRender compositor для прозрачности неактивных
    // окон, без picom. Не критично для запуска: если COMPOSITE/RENDER
    // недоступны на сервере, Vexland продолжает работать без
    // прозрачности (VXR::isAvailable() вернёт false, все вызовы
    // registerWindow/setWindowOpacity/repaint становятся no-op).
    VXR::init(m_conn, m_root);

    initManagers(STAGE_PRIORITY);

    // конфиг грузим сразу после ConfigManager создан, до остальных
    // систем — так gaps/прочие значения доступны с первого recalculate
    if (!configPath.empty()) {
        if (!CConfigManager::get()->load(configPath))
            std::println(stderr, "[ WARN ] Config load failed: {} — continuing with defaults", CConfigManager::get()->lastError());
    }

    // Splash — ПОСЛЕ загрузки конфига, чтобы general.disable_splash
    // (если пользователь его выставил) сработал с самого начала, а не
    // с одного кадра задержкой.
    Splash::init();

    if (m_onlyConfigVerification)
        return true; // для --verify-config режима дальше идти не нужно

    initManagers(STAGE_BASICINIT);
    initManagers(STAGE_LATE);

    m_initialized = true;
    std::println("[ INFO ] Compositor initialized");

    return true;
}

void CCompositor::stopCompositor() {
    m_isShuttingDown = true;
}

void CCompositor::cleanup() {
    if (!m_conn)
        return;

    m_isShuttingDown = true;

    // TODO: явная очистка реестров (WindowState/WorkspaceState/
    // MonitorState) — сейчас они синглтоны с static локальным
    // instance, живущие до конца процесса, так что explicit clear()
    // не критичен при простом exit(), но станет нужен если когда-то
    // добавим "мягкий рестарт" WM без перезапуска процесса

    xcb_disconnect(m_conn);
    m_conn = nullptr;
}

void CCompositor::startCompositor() {
    // сам event loop живёт в main.cpp (xcb_poll_for_event цикл) —
    // этот метод исторически называется как в оригинале, но у нас
    // не блокирует поток сам, просто финальный лог перед стартом
    std::println("[ INFO ] Compositor ready, entering event loop");
}
