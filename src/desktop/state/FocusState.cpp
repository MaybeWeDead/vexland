#include "FocusState.hpp"
#include "../view/WindowState.hpp"
#include "../../config/ConfigManager.hpp"

extern "C" {
#include <xcb/xcb.h>
}

#include <cstdio>
#include <cstdlib>
#include <print>

// -----------------------------------------------------------------------
// FocusState.cpp — window() резолвит через реестр (безопасно на случай
// исчезнувшего окна). fullWindowFocus реально дёргает X11 через
// xcb_set_input_focus — без этого клавиатурный ввод не будет доходить
// до нужного окна вообще, вне зависимости от того, что мы думаем
// "должно быть сфокусировано" на уровне нашей внутренней структуры.
//
// Border-подсветка (active/inactive) — простейший X11 core-protocol
// border, БЕЗ compositing. Аналог general { col.active_border,
// col.inactive_border, border_size } из Hyprland-конфига, только
// прочитанное через тот же generic cfg->getString/getFloat, что уже
// используется для gaps_in (см. Algorithm.cpp) — свою типизированную
// конфиг-структуру мы сознательно не заводим, см. комментарий в
// Space.cpp про "аналог CConfigValue<>".
// -----------------------------------------------------------------------

namespace {

// "rrggbb" -> packed 0xRRGGBB для XCB_CW_BORDER_PIXEL. Не парсим alpha —
// X11 core border не умеет composite-прозрачность без picom, так что
// 4-й байт тут бессмысленен (в отличие от _NET_WM_WINDOW_OPACITY).
uint32_t parseHexColor(const std::string& hex, uint32_t fallback) {
    std::string s = hex;
    if (!s.empty() && s[0] == '#')
        s.erase(0, 1);

    if (s.size() != 6)
        return fallback;

    char*    end   = nullptr;
    uint32_t value = std::strtoul(s.c_str(), &end, 16);
    if (end != s.c_str() + 6)
        return fallback;

    return value;
}

void applyBorder(xcb_connection_t* conn, PHLWINDOW w, bool active) {
    if (!conn || !w || !w->m_isMapped) {
        std::println("[ BORDERDEBUG ] applyBorder SKIPPED: conn={} w={} mapped={}", conn != nullptr, w != nullptr, w ? w->m_isMapped : false);
        return;
    }

    auto* cfg = CConfigManager::get();

    const double borderSize = cfg ? cfg->getFloat("general.border_size", 2.0) : 2.0;

    const std::string colorKey = active ? "general.col.active_border" : "general.col.inactive_border";
    const std::string defaultColor = active ? "88c0d0" : "3b4252"; // нейтральные дефолты, легко переопределить в конфиге

    const uint32_t colorPixel = parseHexColor(cfg ? cfg->getString(colorKey, defaultColor) : defaultColor,
                                                parseHexColor(defaultColor, 0x888888));

    const uint32_t borderWidth = static_cast<uint32_t>(borderSize < 0 ? 0 : borderSize);

    std::println("[ BORDERDEBUG ] applyBorder: xwin={} active={} borderSize={} width={} colorPixel=0x{:06x}",
                 w->getX11Window(), active, borderSize, borderWidth, colorPixel);

    const uint32_t configValues[] = {borderWidth};
    auto cookie1 = xcb_configure_window_checked(conn, w->getX11Window(), XCB_CONFIG_WINDOW_BORDER_WIDTH, configValues);
    auto err1 = xcb_request_check(conn, cookie1);
    if (err1) {
        std::println("[ BORDERDEBUG ] xcb_configure_window FAILED: error_code={}", err1->error_code);
        free(err1);
    }

    const uint32_t attrValues[] = {colorPixel};
    auto cookie2 = xcb_change_window_attributes_checked(conn, w->getX11Window(), XCB_CW_BORDER_PIXEL, attrValues);
    auto err2 = xcb_request_check(conn, cookie2);
    if (err2) {
        std::println("[ BORDERDEBUG ] xcb_change_window_attributes FAILED: error_code={}", err2->error_code);
        free(err2);
    }

    xcb_flush(conn);
}

} // namespace

PHLWINDOW CFocusState::window() const {
    if (m_focusedWindowID == 0)
        return nullptr;

    return CWindowState::get()->byID(m_focusedWindowID);
}

void CFocusState::fullWindowFocus(xcb_connection_t* conn, PHLWINDOW w, eFocusReason reason) {
    (void)reason; // TODO: пробросить в будущий EventBus/IPC когда появится,
                  // сейчас просто логируем факт смены фокуса без деталей причины

    // запоминаем старое окно ДО перезаписи m_focusedWindowID, чтобы
    // перекрасить его рамку в inactive ниже
    const auto prevWindow = window();

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

        if (prevWindow)
            applyBorder(conn, prevWindow, /*active=*/false);

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

    // перекрашиваем рамки: старое окно -> inactive, новое -> active.
    // Если prevWindow == w (повторный фокус на то же окно), просто
    // перерисуем active ещё раз — идемпотентно, не страшно.
    if (prevWindow && prevWindow != w)
        applyBorder(conn, prevWindow, /*active=*/false);

    applyBorder(conn, w, /*active=*/true);
}
