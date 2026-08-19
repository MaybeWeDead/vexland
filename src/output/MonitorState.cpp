#include "MonitorState.hpp"

extern "C" {
#include <xcb/randr.h>
}

#include <cstdio>
#include <print>
#include <cstring>

// -----------------------------------------------------------------------
// MonitorState.cpp — реальный опрос через XRandR, паттерн по референсу
// libkscreen (KDE): screen_resources -> перебор outputs -> для каждого
// подключённого output с активным CRTC берём crtc_info (там x/y/width/
// height) -> строим CMonitor.
//
// НЕ используется устаревший xcb_randr_get_screen_resources (v1.0) —
// берём _current вариант, который не форсирует полный re-probe хардвера
// на каждый вызов (тяжелее и может мигнуть экраном на некоторых драйверах).
// -----------------------------------------------------------------------

bool CMonitorState::refresh(xcb_connection_t* conn, xcb_window_t root) {
    m_monitors.clear();

    xcb_randr_get_screen_resources_current_cookie_t resCookie = xcb_randr_get_screen_resources_current(conn, root);
    xcb_randr_get_screen_resources_current_reply_t*  resReply = xcb_randr_get_screen_resources_current_reply(conn, resCookie, nullptr);

    if (!resReply) {
        std::println(stderr, "[ ERROR ] XRandR: failed to get screen resources");
        return false;
    }

    xcb_randr_output_t* outputs    = xcb_randr_get_screen_resources_current_outputs(resReply);
    int                  numOutputs = xcb_randr_get_screen_resources_current_outputs_length(resReply);

    for (int i = 0; i < numOutputs; ++i) {
        xcb_randr_get_output_info_cookie_t outCookie = xcb_randr_get_output_info(conn, outputs[i], resReply->config_timestamp);
        xcb_randr_get_output_info_reply_t*  outReply  = xcb_randr_get_output_info_reply(conn, outCookie, nullptr);

        if (!outReply) {
            continue; // не крашимся на единичной ошибке — просто пропускаем этот output
        }

        // пропускаем отключенные мониторы и те, у которых нет активного CRTC
        // (CRTC = "controller", физически рисующий картинку на выходе)
        if (outReply->connection != XCB_RANDR_CONNECTION_CONNECTED || outReply->crtc == XCB_NONE) {
            free(outReply);
            continue;
        }

        xcb_randr_get_crtc_info_cookie_t crtcCookie = xcb_randr_get_crtc_info(conn, outReply->crtc, resReply->config_timestamp);
        xcb_randr_get_crtc_info_reply_t*  crtcReply  = xcb_randr_get_crtc_info_reply(conn, crtcCookie, nullptr);

        if (!crtcReply) {
            free(outReply);
            continue;
        }

        // имя output'а — не null-terminated строка, длина отдельно
        const char* nameData = reinterpret_cast<const char*>(xcb_randr_get_output_info_name(outReply));
        int          nameLen  = xcb_randr_get_output_info_name_length(outReply);
        std::string  name(nameData, nameLen);

        auto monitor = CMonitor::create(outputs[i], outReply->crtc, name, crtcReply->x, crtcReply->y, crtcReply->width, crtcReply->height);

        m_monitors.push_back(monitor);

        std::println("[ INFO ] monitor found: {} at {},{} {}x{}", name, crtcReply->x, crtcReply->y, crtcReply->width, crtcReply->height);

        free(crtcReply);
        free(outReply);
    }

    free(resReply);

    if (m_monitors.empty())
        std::println(stderr, "[ WARN ] XRandR refresh found no connected monitors — is a display actually attached?");

    return !m_monitors.empty();
}
