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

    // 1. Читаем текущие атрибуты окна, чтобы узнать, какие маски оно просило само
    xcb_get_window_attributes_cookie_t cookie = xcb_get_window_attributes(m_conn, window);
    xcb_get_window_attributes_reply_t* reply = xcb_get_window_attributes_reply(m_conn, cookie, nullptr);

    uint32_t current_mask = 0;
    if (reply) {
        current_mask = reply->your_event_mask;
        free(reply);
    }

    // 2. Добавляем наши маски (ввод мышью) к существующим
    current_mask |= XCB_EVENT_MASK_ENTER_WINDOW;
    current_mask |= XCB_EVENT_MASK_BUTTON_PRESS;
    current_mask |= XCB_EVENT_MASK_FOCUS_CHANGE;

    // 3. Записываем обновленную маску обратно
    uint32_t mask = XCB_CW_EVENT_MASK;
    xcb_change_window_attributes(m_conn, window, mask, &current_mask);
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
