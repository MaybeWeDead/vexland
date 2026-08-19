#pragma once

#include <sys/resource.h>
#include <string>
#include <optional>
#include <memory>

extern "C" {
#include <xcb/xcb.h>
}

// -----------------------------------------------------------------------
// Compositor.hpp — аналог CCompositor (g_pCompositor). Тонкая "точка
// сборки" всех уже написанных систем (WindowState, WorkspaceState,
// MonitorState, FocusState, LayoutManager, ConfigManager,
// CKeybindManager) — не хранит бизнес-логику сам, только владеет
// соединением к X-серверу и координирует запуск/остановку.
//
// Осознанно НЕ перенесено: wl_display/wl_event_loop (Wayland event
// loop — у нас свой в main.cpp, простой xcb_poll_for_event цикл),
// Aquamarine::CBackend (это wlroots-подобный DRM/KMS рендер-backend —
// у X11 эта роль у самого X-сервера, WM не рендерит), drm.fd/syncobj
// (explicit sync для Wayland-композиторов — неприменимо к X11 модели,
// где WM не композитит сам).
//
// Перенесено по духу: трёхфазная инициализация менеджеров
// (STAGE_PRIORITY/BASICINIT/LATE — разумный паттерн порядка
// зависимостей, используем тот же), bumpNofile/restoreNofile (лимиты
// файловых дескрипторов, полезно для долгоживущего процесса типа WM
// вне зависимости от display-протокола), getVTNr.
// -----------------------------------------------------------------------

enum eManagersInitStage : uint8_t {
    STAGE_PRIORITY = 0, // критичные системы, без которых ничего не работает (X11 connection, ConfigManager)
    STAGE_BASICINIT,    // реестры (WindowState, WorkspaceState, MonitorState, FocusState)
    STAGE_LATE,         // всё, что зависит от предыдущих стадий (LayoutManager registration, KeybindManager)
};

class CCompositor {
  public:
    explicit CCompositor(bool onlyConfigVerification = false);
    ~CCompositor();

    xcb_connection_t* m_conn = nullptr;
    xcb_window_t       m_root = 0;

    bool m_initialized             = false;
    bool m_safeMode                = false;
    bool m_sessionActive            = true;
    bool m_isShuttingDown           = false;
    bool m_onlyConfigVerification   = false;

    std::string m_explicitConfigPath;
    std::string m_instanceSignature;
    std::string m_instancePath;

    // запускает X11-подключение + все менеджеры по стадиям —
    // аналог initServer, но без Wayland socket handover логики
    bool init(const std::string& configPath);

    // основной цикл — вызывается из main.cpp после успешного init()
    void startCompositor();
    void stopCompositor();
    void cleanup();

    void bumpNofile();
    void restoreNofile();

    std::optional<unsigned int> getVTNr() const;

  private:
    void initManagers(eManagersInitStage stage);
    void performUserChecks();

    rlimit m_originalNofile = {};
};

inline std::unique_ptr<CCompositor> g_pCompositor;
