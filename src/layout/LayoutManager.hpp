#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include "space/Space.hpp"
#include "target/Target.hpp"

// -----------------------------------------------------------------------
// LayoutManager.hpp — аналог Layout::CLayoutManager. Тонкий диспетчер:
// не хранит layout-логику сам (это в CSpace/CDwindleAlgorithm), только
// маршрутизирует вызовы + держит центральный реестр Space'ов по
// workspace ID (наша замена монитор/воркспейс-иерархии оригинала).
//
// eRectCorner/edgeTop.../cornerFromBox перенесены как есть — это
// чистая геометрия, не завязана на Wayland вообще.
//
// Осознанно НЕ перенесено (пока): drag/snap система (CDragStateController,
// performSnap), moveInDirection/switchTargets (нужен Math::eDirection
// парсинг и focus tracking, которых пока нет полноценно) — вернутся
// вместе с InputManager, когда будет реальный mouse/keyboard tracking.
// -----------------------------------------------------------------------

enum eRectCorner : uint8_t {
    CORNER_NONE        = 0,
    CORNER_TOP         = (1 << 0),
    CORNER_BOTTOM      = (1 << 1),
    CORNER_LEFT        = (1 << 2),
    CORNER_RIGHT       = (1 << 3),
    CORNER_TOPLEFT     = CORNER_TOP | CORNER_LEFT,
    CORNER_TOPRIGHT    = CORNER_TOP | CORNER_RIGHT,
    CORNER_BOTTOMRIGHT = CORNER_BOTTOM | CORNER_RIGHT,
    CORNER_BOTTOMLEFT  = CORNER_BOTTOM | CORNER_LEFT,
};

inline bool edgeTop(int corner) {
    return corner & CORNER_TOP;
}
inline bool edgeBottom(int corner) {
    return corner & CORNER_BOTTOM;
}
inline bool edgeLeft(int corner) {
    return corner & CORNER_LEFT;
}
inline bool edgeRight(int corner) {
    return corner & CORNER_RIGHT;
}

inline eRectCorner cornerFromBox(const SBox& box, const SVec2& pos) {
    const SVec2 center{box.x + box.w / 2.0, box.y + box.h / 2.0};

    if (pos.x < center.x)
        return pos.y < center.y ? CORNER_TOPLEFT : CORNER_BOTTOMLEFT;
    return pos.y < center.y ? CORNER_TOPRIGHT : CORNER_BOTTOMRIGHT;
}

enum eSnapEdge : uint8_t {
    SNAP_INVALID = 0,
    SNAP_UP      = (1 << 0),
    SNAP_DOWN    = (1 << 1),
    SNAP_LEFT    = (1 << 2),
    SNAP_RIGHT   = (1 << 3),
};

class CLayoutManager {
  public:
    static CLayoutManager* get() {
        static CLayoutManager instance;
        return &instance;
    }

    enum eRecalculateMonitorReason : uint8_t {
        RECALCULATE_MONITOR_REASON_UNKNOWN,
        RECALCULATE_MONITOR_REASON_WORKSPACE_CHANGE,
        RECALCULATE_MONITOR_REASON_TOGGLE_SPECIAL_WORKSPACE,
        RECALCULATE_MONITOR_REASON_TOGGLE_FULLSCREEN,
    };

    void registerSpace(int workspaceID, std::shared_ptr<CSpace> space) {
        m_spaces[workspaceID] = space;
    }

    std::shared_ptr<CSpace> space(int workspaceID) const {
        auto it = m_spaces.find(workspaceID);
        return it == m_spaces.end() ? nullptr : it->second;
    }

    void newTarget(std::shared_ptr<ITarget> target, std::shared_ptr<CSpace> space) {
        if (!target || !space)
            return;
        target->assignToSpace(space);
    }

    void removeTarget(std::shared_ptr<ITarget> target) {
        if (!target)
            return;
        target->assignToSpace(nullptr);
    }

    void changeFloatingMode(std::shared_ptr<ITarget> target) {
        if (!target || !target->space())
            return;
        target->space()->toggleTargetFloating(target);
    }

    void resizeTarget(const SVec2& delta, std::shared_ptr<ITarget> target, eRectCorner corner = CORNER_NONE) {
        if (!target)
            return;

        if (target->isPseudo()) {
            SVec2 fixedDelta = delta;
            if (edgeLeft(corner))
                fixedDelta.x = -fixedDelta.x;
            if (edgeTop(corner))
                fixedDelta.y = -fixedDelta.y;

            SVec2 newPseudo = target->pseudoSize();
            newPseudo.x += fixedDelta.x;
            newPseudo.y += fixedDelta.y;
            target->setPseudoSize(newPseudo);
            return;
        }

        if (target->space())
            target->space()->resizeTarget(delta, target);
    }

    void moveTarget(const SVec2& delta, std::shared_ptr<ITarget> target) {
        if (!target || !target->floating() || !target->space())
            return;
        target->space()->moveTarget(delta, target);
    }

    void setTargetGeom(const SBox& box, std::shared_ptr<ITarget> target) {
        if (!target || !target->floating() || !target->space())
            return;
        target->space()->setTargetGeom(box, target);
    }

    void switchTargets(std::shared_ptr<ITarget> a, std::shared_ptr<ITarget> b) {
        if (!a || !b)
            return;
        a->swap(b);
    }

    std::optional<SVec2> predictSizeForNewTiledTarget() {
        // TODO: требует знания текущего активного монитора/workspace,
        // которое даст FocusState (ещё не написан) — заглушка пока
        return std::nullopt;
    }

    void recalculateMonitor(int workspaceID, eRecalculateMonitorReason reason = RECALCULATE_MONITOR_REASON_UNKNOWN) {
        auto s = space(workspaceID);
        if (!s)
            return;

        eRecalculateReason mapped = RECALCULATE_REASON_UNKNOWN;
        switch (reason) {
            case RECALCULATE_MONITOR_REASON_WORKSPACE_CHANGE: mapped = RECALCULATE_REASON_WORKSPACE_CHANGE; break;
            default: break;
        }

        s->recalculate(mapped);
    }

    void invalidateMonitorGeometries(int workspaceID, double x, double y, double w, double h) {
        auto s = space(workspaceID);
        if (!s)
            return;

        s->setMonitorGeometry(x, y, w, h);
        s->recalculate(RECALCULATE_REASON_INVALIDATE_MONITOR_GEOMETRIES);
    }

  private:
    CLayoutManager()  = default;
    ~CLayoutManager() = default;

    std::unordered_map<int, std::shared_ptr<CSpace>> m_spaces;
};
