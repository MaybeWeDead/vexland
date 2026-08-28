#pragma once

extern "C" {
#include <xcb/xcb.h>
}

#include "../desktop/state/FocusState.hpp"

// -----------------------------------------------------------------------
// InputManager.hpp — менеджер ввода (мышь, фокус по наведению, drag/resize)
// В Hyprland это монстр на 2000 строк, тут мы оставляем только то, что 
// реально делает X11: перехват EnterNotify для FFM и ButtonPress для мува.
// -----------------------------------------------------------------------
class CInputManager {
  public:
    static CInputManager* get() {
        static CInputManager instance;
        return &instance;
    }

    // Вызывается из main.cpp при инициализации, чтобы менеджер знал коннект
    void init(xcb_connection_t* conn, xcb_window_t root);

    // Вызывается из main.cpp в onMapRequest. 
    // ВАЖНО: добавляет маски ввода к окну, не затирая те, что приложение просило само!
    void onWindowCreated(xcb_window_t window);

    // Главный диспетчер событий мыши. Вызывается из event loop.
    void onMouseEvent(xcb_generic_event_t* event);

  private:
    CInputManager() = default;

    void handleEnterNotify(xcb_enter_notify_event_t* ev);
    void handleButtonPress(xcb_button_press_event_t* ev);

    xcb_connection_t* m_conn = nullptr;
    xcb_window_t      m_root = XCB_WINDOW_NONE;
};
