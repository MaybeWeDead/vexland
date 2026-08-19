#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <xcb/xcb.h>

// -----------------------------------------------------------------------
// Window.hpp — X11/XCB реализация. Структура и набор возможностей
// вдохновлены Hyprland (group/swallow/wobble/alpha states/rules), но
// без единой Wayland-зависимости. Окно рождается из xcb_window_t,
// а не из CXWaylandSurface/CXDGSurfaceResource.
// -----------------------------------------------------------------------

class CWindow;
using PHLWINDOW = std::shared_ptr<CWindow>;
using WPWINDOW  = std::weak_ptr<CWindow>;

// Многослойный alpha-стейт как в Hyprland — разные системы (fade,
// fullscreen, layout-transition) двигают alpha независимо, финальная
// видимая прозрачность — их произведение. Держим как простые float'ы,
// анимирует их наш собственный AnimationManager через configure_window
// + сюда же потом можно повесить _NET_WM_WINDOW_OPACITY на будущее.
enum class eWindowAlpha : uint8_t {
    FADE = 0,
    ACTIVE,
    FULLSCREEN,
    LAYOUT,
    MOVE_TO_WORKSPACE,
    MOVE_FROM_WORKSPACE,
    LAST,
};

struct SWindowAnimState {
    // текущее/целевое положение и размер — интерполируется на каждый tick
    double curX = 0, curY = 0, curW = 1, curH = 1;
    double goalX = 0, goalY = 0, goalW = 1, goalH = 1;
    double beginX = 0, beginY = 0, beginW = 1, beginH = 1;

    float  alpha[static_cast<size_t>(eWindowAlpha::LAST)] = {1, 1, 1, 1, 1, 1};

    bool   animating = false;
};

// Группа окон (аналог CGroup в Hyprland) — набор ID окон + текущий активный
struct SWindowGroup {
    std::vector<uint64_t> windowIDs;
    uint64_t              activeID = 0;
    bool                  locked   = false;
};

class CWindow : public std::enable_shared_from_this<CWindow> {
  public:
    // Создание — единственный источник истины: реальный X11 window ID,
    // полученный из MapRequest в event loop. Никаких Wayland-объектов.
    static PHLWINDOW create(xcb_connection_t* conn, xcb_window_t xwin);

    ~CWindow();

    // --- идентичность / X11 handle ---
    xcb_window_t getX11Window() const {
        return m_xwin;
    }
    uint64_t stableID() const {
        return m_stableID;
    }

    // --- геометрия (текущая — то, что реально нарисовано на экране) ---
    void setGoalGeometry(double x, double y, double w, double h, bool warp = false);
    void applyCurrentGeometryToX11(); // configure_window вызов

    double x() const {
        return m_anim.curX;
    }
    double y() const {
        return m_anim.curY;
    }
    double w() const {
        return m_anim.curW;
    }
    double h() const {
        return m_anim.curH;
    }

    // --- alpha / fade (значения меняет наш AnimationManager, WM их
    // только читает/задаёт цель; фактическую картинку рисует picom) ---
    float  alpha(eWindowAlpha type) const;
    void   setAlphaGoal(eWindowAlpha type, float goal);

    // --- state flags, аналог Hyprland-флагов на m_bIsFloating и т.д. ---
    bool m_isFloating  = false;
    bool m_isMapped    = false;
    bool m_isFullscreen = false;
    bool m_pinned      = false;
    bool m_hidden      = false;

    // --- metadata (аналог m_class/m_title) ---
    std::string m_class;
    std::string m_title;
    pid_t       m_pid = -1;

    // --- workspace / monitor привязка (ID-based, не указатели —
    // чтобы не словить dangling pointer, если workspace исчезнет) ---
    int m_workspaceID = -1;
    int m_monitorID   = -1;

    // --- group (аналог CGroup, но через ID вместо shared_ptr на группу
    // напрямую, WM хранит группы централизованно и резолвит по ID) ---
    std::optional<uint64_t> m_groupID;

    // --- swallow (аналог m_swallowee) — тоже через ID, не через
    // прямой shared_ptr, чтобы не держать окно живым дольше нужного ---
    std::optional<uint64_t> m_swalloweeID;
    bool                    m_hasSwallower = false;

    // --- wobble — параметры для эффекта, который вычисляет наш
    // собственный transformer на основе delta позиции (см. AnimationManager) ---
    struct {
        double offsetX = 0, offsetY = 0;
        bool   active = false;
    } m_wobble;

    // --- rules (аналог CWindowRuleApplicator, упрощённо: просто
    // список применённых строковых правил + помощники, пока без
    // отдельного applicator-класса — добавим когда будет конфиг-парсер) ---
    std::vector<std::string> m_appliedRules;

  private:
    CWindow(xcb_connection_t* conn, xcb_window_t xwin);

    xcb_connection_t* m_conn = nullptr;
    xcb_window_t      m_xwin = 0;
    uint64_t          m_stableID;

    SWindowAnimState  m_anim;

    static uint64_t   s_idCounter;
};
