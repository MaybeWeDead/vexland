#include "FullscreenController.hpp"
#include "../layout/LayoutManager.hpp"
#include "../desktop/Workspace.hpp"
#include "../state/WorkspaceState.hpp"

#include <cstring>
#include <cstdio>
#include <print>

void CFullscreenController::initAtoms(xcb_connection_t* conn) {
    if (m_atomsInitialized)
        return;

    xcb_intern_atom_cookie_t c1 = xcb_intern_atom(conn, 0, strlen("_NET_WM_STATE"), "_NET_WM_STATE");
    xcb_intern_atom_reply_t* r1 = xcb_intern_atom_reply(conn, c1, nullptr);
    if (r1) { m_atomWmState = r1->atom; free(r1); }

    xcb_intern_atom_cookie_t c2 = xcb_intern_atom(conn, 0, strlen("_NET_WM_STATE_FULLSCREEN"), "_NET_WM_STATE_FULLSCREEN");
    xcb_intern_atom_reply_t* r2 = xcb_intern_atom_reply(conn, c2, nullptr);
    if (r2) { m_atomFullscreen = r2->atom; free(r2); }

    m_atomsInitialized = true;
}

void CFullscreenController::setFullscreenMode(xcb_connection_t* conn, PHLWINDOW w, bool fullscreen) {
    if (!conn || !w)
        return;

    initAtoms(conn);

    if (m_atomWmState == XCB_ATOM_NONE || m_atomFullscreen == XCB_ATOM_NONE) {
        std::println(stderr, "[ ERROR ] FullscreenController: Failed to get X11 atoms");
        return;
    }

    // 1. Обновляем внутренний флаг окна (аналог m_isFullscreen в Hyprland)
    w->m_isFullscreen = fullscreen;

    // 2. Отправляем EWMH-сообщение X-серверу (аналог window->backend().setFullscreen())
    xcb_client_message_event_t ev;
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.window = w->getX11Window();
    ev.type = m_atomWmState;
    ev.format = 32;
    ev.data.data32[0] = fullscreen ? 1 : 0; // 1 = _NET_WM_STATE_ADD, 0 = _NET_WM_STATE_REMOVE
    ev.data.data32[1] = m_atomFullscreen;
    ev.data.data32[2] = 0;

    // Отправляем в root window
    const xcb_setup_t* setup = xcb_get_setup(conn);
    xcb_screen_t* screen = xcb_setup_roots_iterator(setup).data;

    xcb_send_event(conn, 1, screen->root, XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT, (const char*)&ev);
    xcb_flush(conn);

    // 3. Даем команду LayoutManager пересчитать воркспейс (как в оригинале)
    // В Hyprland это WORKSPACE->m_space->recalculate(...)
    if (w->m_workspaceID != -1) {
        auto ws = CWorkspaceState::get()->byID(w->m_workspaceID);
        if (ws) {
            ws->updateWindows(); // Пересчитываем геометрию с учетом фуллскрина
        }
    }

    std::println("[ INFO ] Fullscreen state for window {} set to {}", w->stableID(), fullscreen);
}
