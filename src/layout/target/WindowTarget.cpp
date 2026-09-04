#include "WindowTarget.hpp"
#include "../space/Space.hpp"
#include "../algorithm/Algorithm.hpp"

#include <algorithm>
#include <cmath>

// -----------------------------------------------------------------------
// WindowTarget.cpp — аналог CWindowTarget.cpp. updatePos() — сердце
// файла: превращает абстрактный box, который дал layout-алгоритм,
// в реальную geometry окна.
// Gaps уже применяются в CDwindleAlgorithm::recalcNode(), поэтому
// здесь второй раз их НЕ вычитаем.
// Fullscreen и group-handling из оригинала здесь сознательно опущены.
// -----------------------------------------------------------------------

std::shared_ptr<ITarget> CWindowTarget::create(PHLWINDOW w) {
    auto target = std::shared_ptr<CWindowTarget>(new CWindowTarget(w));
    return target;
}

CWindowTarget::CWindowTarget(PHLWINDOW w) : m_window(w) {}

void CWindowTarget::setPositionGlobal(const STargetBox& box, uint8_t flags) {
    ITarget::setPositionGlobal(box, flags);
    updatePos(flags);
}

// Аналог updatePos() из оригинала, упрощённый: без fullscreen/group
// veto-путей. Layout algorithm уже отдаёт box с учётом gaps_in,
// поэтому updatePos() занимается только финальной обработкой окна.
void CWindowTarget::updatePos(uint8_t flags) {
    (void)flags;

    auto win = m_window.lock();
    if (!win)
        return;

    if (!m_space)
        return;

    SBox nodeBox = m_box.logicalBox;

    SVec2 calcPos{nodeBox.x, nodeBox.y};
    SVec2 calcSize{nodeBox.w, nodeBox.h};

    // Floating окна получают свой box как есть.
    if (floating()) {
        win->setGoalGeometry(
            std::round(calcPos.x),
            std::round(calcPos.y),
            std::round(calcSize.x),
            std::round(calcSize.y)
        );
        return;
    }

    // ВАЖНО:
    // gaps_in уже применены в CDwindleAlgorithm::recalcNode().
    // Не вычитаем их здесь повторно.

    // --- pseudo-tiling ---------------------------------------------------
    // Аналог блока isPseudo() в оригинале: окно хочет фиксированный
    // размер внутри выделенного ему тайла — центрируем, при
    // необходимости масштабируем вниз, если тайл меньше желаемого.
    if (isPseudo()) {
        double scale = 1.0;

        if (m_pseudoSize.x > calcSize.x || m_pseudoSize.y > calcSize.y) {
            if (m_pseudoSize.x > calcSize.x)
                scale = calcSize.x / m_pseudoSize.x;
            if (m_pseudoSize.y * scale > calcSize.y)
                scale = calcSize.y / m_pseudoSize.y;

            const SVec2 scaledSize{
                m_pseudoSize.x * scale,
                m_pseudoSize.y * scale
            };
            const SVec2 delta{
                calcSize.x - scaledSize.x,
                calcSize.y - scaledSize.y
            };

            calcPos.x += delta.x / 2.0;
            calcPos.y += delta.y / 2.0;
            calcSize = scaledSize;
        } else {
            const SVec2 delta{
                calcSize.x - m_pseudoSize.x,
                calcSize.y - m_pseudoSize.y
            };

            calcPos.x += delta.x / 2.0;
            calcPos.y += delta.y / 2.0;
            calcSize = m_pseudoSize;
        }
    }

    // --- min/max size clamp ----------------------------------------------
    if (const auto minS = minSize()) {
        calcSize.x = std::max(calcSize.x, minS->x);
        calcSize.y = std::max(calcSize.y, minS->y);
    }

    if (const auto maxS = maxSize()) {
        calcSize.x = std::min(calcSize.x, maxS->x);
        calcSize.y = std::min(calcSize.y, maxS->y);
    }

    // Layout применяется через анимацию (AnimationManager тикает
    // cur* -> goal* на каждый кадр, см. managers/AnimationManager.cpp).
    //
    // Раньше здесь стоял warp=true, потому что AnimationManager ещё не
    // существовал: warp=false менял только goal* и оставлял cur* на
    // старой геометрии навсегда, никто его не продвигал. Теперь тикер
    // существует, так что warp=false корректно анимирует layout-
    // изменения — тот самый "плавный dwindle resize", ради которого
    // вся структура SWindowAnimState была заведена изначально.
    win->setGoalGeometry(
        std::round(calcPos.x),
        std::round(calcPos.y),
        std::round(calcSize.x),
        std::round(calcSize.y),
        /*warp=*/false
    );
}

void CWindowTarget::assignToSpace(std::shared_ptr<CSpace> space, std::optional<SVec2> focalPoint) {
    if (!space) {
        ITarget::assignToSpace(space, focalPoint);
        return;
    }

    auto win = m_window.lock();
    if (!win)
        return;

    win->m_workspaceID = space->workspaceID();

    ITarget::assignToSpace(space, focalPoint);
}

bool CWindowTarget::floating() {
    auto win = m_window.lock();
    return win ? win->m_isFloating : false;
}

void CWindowTarget::setFloating(bool x) {
    auto win = m_window.lock();
    if (!win || x == win->m_isFloating)
        return;

    win->m_isFloating = x;
    win->m_pinned     = false;
}

SVec2 CWindowTarget::clampSizeForDesired(const SVec2& size) const {
    auto win = m_window.lock();
    if (!win)
        return size;

    SVec2 result = size;
    // TODO: подключить min/maxSize() окна, когда появятся size hints
    // из ICCCM (WM_NORMAL_HINTS) — сейчас Window.hpp их не хранит
    return result;
}

std::expected<SGeometryRequested, eGeometryFailure> CWindowTarget::desiredGeometry() {
    auto win = m_window.lock();
    if (!win)
        return std::unexpected(GEOMETRY_NO_DESIRED);

    SGeometryRequested requested;
    requested.size = SVec2{win->w(), win->h()};
    requested.pos  = SVec2{win->x(), win->y()};

    if (requested.size.x <= 2 || requested.size.y <= 2)
        return std::unexpected(GEOMETRY_NO_DESIRED);

    return requested;
}

PHLWINDOW CWindowTarget::window() const {
    return m_window.lock();
}

std::optional<SVec2> CWindowTarget::minSize() {
    // TODO: ICCCM WM_NORMAL_HINTS — пока нет источника этих данных
    return std::nullopt;
}

std::optional<SVec2> CWindowTarget::maxSize() {
    return std::nullopt;
}

void CWindowTarget::damageEntire() {
    // В X11-модели явное damage не требуется на уровне WM — picom сам
    // ловит XDamage события от X-сервера при configure_window вызовах.
}

void CWindowTarget::warpPositionSize() {
    auto win = m_window.lock();
    if (!win)
        return;

    win->setGoalGeometry(
        win->x(),
        win->y(),
        win->w(),
        win->h(),
        /*warp=*/true
    );
}

void CWindowTarget::onUpdateSpace() {
    auto win = m_window.lock();
    if (!win || !space())
        return;

    win->m_workspaceID = space()->workspaceID();
}
