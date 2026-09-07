#include "InputManager.hpp"
#include "../desktop/view/WindowState.hpp"
#include "../desktop/view/Window.hpp"
#include <cstdio>
#include <print>

void CInputManager::init(xcb_connection_t* conn, xcb_window_t root) {
    m_conn = conn;
    m_root = root;
}

void CInputManager::onWindowCreated(xcb_window_t window) {
    if (!m_conn)
        return;

    // ИЗМЕНЕНО: раньше читали существующий your_event_mask через
    // xcb_get_window_attributes и добавляли к нему наши биты — на
    // alacritty (в отличие от xterm) эта комбинация давала BadValue
    // (X11 ERROR code=10 major=2), что ломало клики на этом окне
    // вообще (без BUTTON_PRESS в маске сервер не шлёт нам это событие
    // — отсюда "не могу переключить фокус кликом на другое окно").
    //
    // Причина комбинации не выяснена до конца (возможна гонка в самой
    // инициализации X11-окна alacritty), но раз читать чужую маску и
    // дополнять её оказалось хрупко — просто ставим СВОЙ набор с нуля,
    // не пытаясь сохранить то, что клиент мог запросить себе сам.
    // Клиентские собственные маски (для рендеринга, resize-событий и
    // т.п.) обычно не пересекаются с тем, что нужно WM для focus-
    // tracking, так что перезапись с нуля здесь безопасна.
    const uint32_t ourMask = XCB_EVENT_MASK_ENTER_WINDOW | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE;

    auto cookie = xcb_change_window_attributes_checked(m_conn, window, XCB_CW_EVENT_MASK, &ourMask);
    if (auto err = xcb_request_check(m_conn, cookie)) {
        std::println(stderr, "[ WARN ] InputManager: failed to set event mask on window {}: code={} major={} minor={}",
                     window, err->error_code, err->major_code, err->minor_code);
        free(err);
    }
}

void CInputManager::onMouseEvent(xcb_generic_event_t* event) {
    if (!event || !m_conn)
        return;

    switch (event->response_type & ~0x80) {
        case XCB_ENTER_NOTIFY:
            handleEnterNotify(reinterpret_cast<xcb_enter_notify_event_t*>(event));
            break;
        case XCB_BUTTON_PRESS:
            handleButtonPress(reinterpret_cast<xcb_button_press_event_t*>(event));
            break;
        default:
            break;
    }
}

void CInputManager::handleEnterNotify(xcb_enter_notify_event_t* ev) {
    // ev->event содержит ID окна, в которое въехал курсор
    auto w = CWindowState::get()->byXWindow(ev->event);
    if (!w || !w->m_isMapped)
        return;

    // Если окно уже в фокусе — ничего не делаем
    if (CFocusState::get()->window() == w)
        return;

    // Передаем фокус новому окну (Focus Follows Mouse)
    CFocusState::get()->fullWindowFocus(m_conn, w, FOCUS_REASON_NEW_WINDOW);
}

void CInputManager::handleButtonPress(xcb_button_press_event_t* ev) {
    // TODO: Здесь будет логика Move/Resize окон мышью
    // Пока просто логируем клики для проверки
    // std::println("[ DEBUG ] Mouse button {} pressed on window {}", ev->detail, ev->event);
}
