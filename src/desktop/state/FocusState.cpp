#include "FocusState.hpp"
#include "../view/WindowState.hpp"

extern "C" {
#include <xcb/xcb.h>
}

#include <cstdio>
#include <print>

// -----------------------------------------------------------------------
// FocusState.cpp — window() резолвит через реестр (безопасно на случай
// исчезнувшего окна). fullWindowFocus реально дёргает X11 через
// xcb_set_input_focus — без этого клавиатурный ввод не будет доходить
// до нужного окна вообще, вне зависимости от того, что мы думаем
// "должно быть сфокусировано" на уровне нашей внутренней структуры.
// -----------------------------------------------------------------------

PHLWINDOW CFocusState::window() const {
    if (m_focusedWindowID == 0)
        return nullptr;

    return CWindowState::get()->byID(m_focusedWindowID);
}

void CFocusState::fullWindowFocus(xcb_connection_t* conn, PHLWINDOW w, eFocusReason reason) {
    (void)reason; // TODO: пробросить в будущий EventBus/IPC когда появится,
                  // сейчас просто логируем факт смены фокуса без деталей причины

    if (!w) {
        // снимаем фокус — отдаём его root window (стандартная практика,
        // не оставлять focus на несуществующем окне, что может привести
        // к тому, что X-сервер продолжит слать события в никуда)
        if (conn) {
            const xcb_setup_t* setup = xcb_get_setup(conn);
            xcb_screen_t*        screen = xcb_setup_roots_iterator(setup).data;
            xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, screen->root, XCB_CURRENT_TIME);
            xcb_flush(conn);
        }

        m_focusedWindowID = 0;
        return;
    }

    if (!w->m_isMapped) {
        std::println(stderr, "[ WARN ] fullWindowFocus: attempted to focus unmapped window {}", w->stableID());
        return;
    }

    m_focusedWindowID = w->stableID();

    if (conn) {
        xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, w->getX11Window(), XCB_CURRENT_TIME);
        xcb_flush(conn);
    }
}
