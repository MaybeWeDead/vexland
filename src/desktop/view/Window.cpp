#include "Window.hpp"
#include "WindowState.hpp"
#include "../../managers/WindowShape.hpp"
#include "../../config/ConfigManager.hpp"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <print>

// -----------------------------------------------------------------------
// Window.cpp — X11/XCB. Поведенческий эквивалент mapWindow/unmapWindow
// из Hyprland, но без единой Wayland-зависимости. Комментарии отмечают,
// какому месту в оригинале соответствует блок кода — так легче сверяться
// при добавлении фич (group/swallow/rules) позже.
// -----------------------------------------------------------------------

uint64_t CWindow::s_idCounter = 1;

// --- конструктор -------------------------------------------------------
// Аналог CWindow::CWindow(SP<CXWaylandSurface>) — но вместо listener'ов
// на Wayland-события у нас просто сохраняем handle. Реакция на реальные
// X11-события (map/unmap/destroy/configure) происходит СНАРУЖИ, в
// event loop (main.cpp), который дёргает нужные методы явно — там нет
// системы подписки на события, как в Wayland, всё диспетчерится вручную.
CWindow::CWindow(xcb_connection_t* conn, xcb_window_t xwin) : m_conn(conn), m_xwin(xwin), m_stableID(s_idCounter++) {
    // геометрию по умолчанию просим у X-сервера чуть позже, в create(),
    // как только объект полностью инициализирован (нельзя дергать
    // shared_from_this() из конструктора)
}

CWindow::~CWindow() {
    // аналог CWindow::~CWindow() — в оригинале там reset фокуса и event
    // эмиты. У нас это на уровне WM/FocusState (появится отдельно),
    // здесь только то, что реально принадлежит объекту.
}

// --- create() ------------------------------------------------------------
// Аналог CWindow::create(SP<CXWaylandSurface>). Ключевое отличие: вместо
// SP<CXWaylandSurface> берём голый xcb_window_t — окно уже существует
// на X-сервере (иначе MapRequest бы не прилетел), мы просто оборачиваем
// его в наш объект и запрашиваем текущую геометрию.
PHLWINDOW CWindow::create(xcb_connection_t* conn, xcb_window_t xwin) {
    if (!conn || !xwin)
        return nullptr;

    // shared_ptr с приватным конструктором — тот же паттерн, что и в
    // оригинале (SP<CWindow>(new CWindow(...))), просто без обёртки SP<>
    PHLWINDOW pWindow(new CWindow(conn, xwin));

    // запрашиваем текущую геометрию окна у X-сервера, чтобы анимация
    // "появления" стартовала от реального места, а не от (0,0) —
    // аналог того, что Hyprland получает из initial commit/configure
    xcb_get_geometry_cookie_t cookie = xcb_get_geometry(conn, xwin);
    xcb_generic_error_t*      err    = nullptr;
    xcb_get_geometry_reply_t* geom   = xcb_get_geometry_reply(conn, cookie, &err);

    if (err) {
        std::println(stderr, "[ WARN ] CWindow::create: xcb_get_geometry failed for window {}, using defaults", xwin);
        free(err);
    }

    if (geom) {
        pWindow->m_anim.curX = pWindow->m_anim.goalX = pWindow->m_anim.beginX = geom->x;
        pWindow->m_anim.curY = pWindow->m_anim.goalY = pWindow->m_anim.beginY = geom->y;
        pWindow->m_anim.curW = pWindow->m_anim.goalW = pWindow->m_anim.beginW = geom->width;
        pWindow->m_anim.curH = pWindow->m_anim.goalH = pWindow->m_anim.beginH = geom->height;
        free(geom);
    } else {
        // разумные дефолты, если геометрию получить не удалось —
        // не даём окну остаться с нулевым размером (что могло бы
        // сломать layout-математику деления на размер)
        pWindow->m_anim.curW = pWindow->m_anim.goalW = pWindow->m_anim.beginW = 640;
        pWindow->m_anim.curH = pWindow->m_anim.goalH = pWindow->m_anim.beginH = 480;
    }

    // подписываемся на события ЭТОГО конкретного окна — нужно, чтобы
    // получать его собственные ConfigureRequest/PropertyNotify и т.д.
    // (аналог того, что Hyprland слушает per-surface события)
    const uint32_t mask   = XCB_CW_EVENT_MASK;
    const uint32_t values = XCB_EVENT_MASK_PROPERTY_CHANGE | XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(conn, xwin, mask, &values);

    return pWindow;
}

// --- geometry ------------------------------------------------------------
// Аналог связки m_realPosition/m_realSize (PHLANIMVAR<Vector2D>) в
// оригинале. У нас это просто числа в SWindowAnimState — AnimationManager
// (отдельный файл) на каждый tick интерполирует cur* к goal* и вызывает
// applyCurrentGeometryToX11().
void CWindow::setGoalGeometry(double x, double y, double w, double h, bool warp) {
    m_anim.beginX = m_anim.curX;
    m_anim.beginY = m_anim.curY;
    m_anim.beginW = m_anim.curW;
    m_anim.beginH = m_anim.curH;

    m_anim.goalX = x;
    m_anim.goalY = y;
    m_anim.goalW = w;
    m_anim.goalH = h;

    if (warp) {
        // мгновенно, без анимации — аналог ->warp() в оригинале,
        // используется например при первом мапе или при noAnim-правиле
        m_anim.curX = x;
        m_anim.curY = y;
        m_anim.curW = w;
        m_anim.curH = h;
        applyCurrentGeometryToX11();
    } else {
        m_anim.animating        = true;
        m_anim.animStartSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

// Аналог того, что AnimationManager делает через xcb_configure_window
// (см. AnimationManager_x11_v1.cpp) — но метод живёт на самом окне,
// потому что окно лучше всех знает свой xcb_window_t и свои текущие
// координаты. AnimationManager просто дёргает этот метод каждый tick.
void CWindow::applyCurrentGeometryToX11() {
    if (!m_conn || !m_xwin)
        return;

    const uint32_t values[4] = {
        (uint32_t)std::lround(m_anim.curX),
        (uint32_t)std::lround(m_anim.curY),
        (uint32_t)std::max(1L, std::lround(m_anim.curW)),
        (uint32_t)std::max(1L, std::lround(m_anim.curH)),
    };

    xcb_configure_window(m_conn, m_xwin, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

    // Пересчитываем скруглённую маску — она привязана к пиксельным
    // размерам окна, значит должна обновляться при каждом resize, не
    // только при первом map(). Fullscreen-окна не скругляем (аналог
    // Hyprland: rounding отключается для fullscreen — рамка на весь
    // экран выглядит чужеродно со скруглением).
    if (!m_isFullscreen) {
        auto* cfg = CConfigManager::get();
        const int rounding = cfg ? static_cast<int>(cfg->getFloat("decoration.rounding", 8.0)) : 8;
        WindowShape::applyRounding(m_conn, shared_from_this(), rounding);
    }
}

// --- alpha -----------------------------------------------------------
// Аналог CWindow::alpha(eWindowAlpha)/alphaValue(). В оригинале это
// PHLANIMVAR<float> с многослойной системой (fade * active * fullscreen
// * layout * moveToWs * moveFromWs = эффективная альфа). У нас — то же
// самое произведение, но без animated-variable обёртки; сама
// интерполяция происходит в AnimationManager, тут только storage.
float CWindow::alpha(eWindowAlpha type) const {
    return m_anim.alpha[static_cast<size_t>(type)];
}

void CWindow::setAlphaGoal(eWindowAlpha type, float goal) {
    // TODO: когда появится AnimationManager для alpha (сейчас там
    // только geometry), тут будет begin/goal пара, как в setGoalGeometry.
    // Пока warp'аем сразу — не блокирует остальную логику.
    m_anim.alpha[static_cast<size_t>(type)] = goal;

    // применяем эффективную (суммарную) альфу через X11 property —
    // picom слушает _NET_WM_WINDOW_OPACITY и сам рисует fade
    if (!m_conn || !m_xwin)
        return;

    float total = 1.0f;
    for (size_t i = 0; i < static_cast<size_t>(eWindowAlpha::LAST); ++i)
        total *= m_anim.alpha[i];

    static xcb_atom_t opacityAtom = XCB_ATOM_NONE;
    if (opacityAtom == XCB_ATOM_NONE) {
        static const char*   name   = "_NET_WM_WINDOW_OPACITY";
        auto                  cookie = xcb_intern_atom(m_conn, 0, strlen(name), name);
        xcb_intern_atom_reply_t* reply = xcb_intern_atom_reply(m_conn, cookie, nullptr);
        if (reply) {
            opacityAtom = reply->atom;
            free(reply);
        }
    }

    if (opacityAtom == XCB_ATOM_NONE)
        return;

    uint32_t opacityVal = static_cast<uint32_t>(std::clamp(total, 0.0f, 1.0f) * 0xFFFFFFFFu);
    xcb_change_property(m_conn, XCB_PROP_MODE_REPLACE, m_xwin, opacityAtom, XCB_ATOM_CARDINAL, 32, 1, &opacityVal);
}
