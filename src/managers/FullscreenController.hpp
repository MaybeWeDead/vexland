#pragma once

extern "C" {
#include <xcb/xcb.h>
}

#include "../desktop/view/Window.hpp"

// -----------------------------------------------------------------------
// FullscreenController.hpp — порт из Hyprland/src/managers/fullscreen/
// Управляет внутренним и клиентским состоянием fullscreen.
// В X11 мы используем EWMH (_NET_WM_STATE_FULLSCREEN) для реального
// разворота окна X-сервером, а здесь храним логику и флаги WM.
// -----------------------------------------------------------------------
class CFullscreenController {
  public:
    static CFullscreenController* get() {
        static CFullscreenController instance;
        return &instance;
    }

    // Аналог setFullscreenMode / setWindowFullscreenModeInternal
    // mode: true = Fullscreen, false = выйти из Fullscreen
    void setFullscreenMode(xcb_connection_t* conn, PHLWINDOW w, bool fullscreen);

  private:
    CFullscreenController() = default;

    // Кэшируем атомы X11, чтобы не запрашивать при каждом вызове
    void initAtoms(xcb_connection_t* conn);

    xcb_atom_t m_atomWmState = XCB_ATOM_NONE;
    xcb_atom_t m_atomFullscreen = XCB_ATOM_NONE;
    bool       m_atomsInitialized = false;
};
