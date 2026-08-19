#include "WindowTarget.hpp"
#include "../space/Space.hpp"
#include "../algorithm/Algorithm.hpp"

#include <algorithm>
#include <cmath>

// -----------------------------------------------------------------------
// WindowTarget.cpp — аналог CWindowTarget.cpp. updatePos() — сердце
// файла: превращает абстрактный box, который дал layout-алгоритм,
// в реальную geometry окна, применяя gaps_in по краям. Fullscreen и
# group-handling из оригинала здесь сознательно опущены — у нас этих
// систем ещё нет, добавим отдельными слоями позже, не усложняя первую
// рабочую версию.
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
// veto-путей. Считает финальный box с учётом gaps_in и pseudo-tiling,
// затем зовёт setGoalGeometry на реальном окне (что запускает анимацию
// перемещения через AnimationManager).
void CWindowTarget::updatePos(uint8_t flags) {
    auto win = m_window.lock();
    if (!win)
        return;

    if (!m_space)
        return;

    SBox nodeBox = m_box.logicalBox;

    SVec2 calcPos{nodeBox.x, nodeBox.y};
    SVec2 calcSize{nodeBox.w, nodeBox.h};

    // floating окна получают свой box как есть, без gaps-логики тайлинга
    if (floating()) {
        win->setGoalGeometry(calcPos.x, calcPos.y, calcSize.x, calcSize.y);
        return;
    }

    // --- gaps_in по краям окна -----------------------------------------
    // аналог GAPOFFSETTOPLEFT/BOTTOMRIGHT из оригинала, упрощённый:
    // фиксированный gaps_in со всех сторон (без per-workspace override
    // и без STICKS-проверки на прилипание к краю монитора — тем самым
    // немного жертвуем визуальной точностью ради простоты первой версии)
    // TODO: подключить сюда значение из конфига, когда появится парсер
    const double gapsIn = 5.0;

    calcPos.x += gapsIn;
    calcPos.y += gapsIn;
    calcSize.x = std::max(1.0, calcSize.x - 2 * gapsIn);
    calcSize.y = std::max(1.0, calcSize.y - 2 * gapsIn);

    // --- pseudo-tiling ---------------------------------------------------
    // аналог блока isPseudo() в оригинале: окно хочет фиксированный
    // размер внутри выделенного ему тайла — центрируем, при
    // необходимости масштабируем вниз, если тайл меньше желаемого
    if (isPseudo()) {
        double scale = 1.0;

        if (m_pseudoSize.x > calcSize.x || m_pseudoSize.y > calcSize.y) {
            if (m_pseudoSize.x > calcSize.x)
                scale = calcSize.x / m_pseudoSize.x;
            if (m_pseudoSize.y * scale > calcSize.y)
                scale = calcSize.y / m_pseudoSize.y;

            const SVec2 scaledSize{m_pseudoSize.x * scale, m_pseudoSize.y * scale};
            const SVec2 delta{calcSize.x - scaledSize.x, calcSize.y - scaledSize.y};

            calcPos.x += delta.x / 2.0;
            calcPos.y += delta.y / 2.0;
            calcSize = scaledSize;
        } else {
            const SVec2 delta{calcSize.x - m_pseudoSize.x, calcSize.y - m_pseudoSize.y};
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

    win->setGoalGeometry(std::round(calcPos.x), std::round(calcPos.y), std::round(calcSize.x), std::round(calcSize.y));

    win->applyCurrentGeometryToX11(); // немедленный apply если warp; иначе AnimationManager подхватит goal на следующем тике
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

    // упрощённая версия: используем текущий размер окна как "желаемый" —
    // в оригинале тут была сложная логика чтения XWayland-геометрии,
    // но у нас окно и так уже X11-нативное, его текущий размер и есть
    // единственный источник истины на момент создания target'а
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
    // в X11-модели явное damage не требуется на уровне WM — picom сам
    // ловит XDamage события от X-сервера при configure_window вызовах;
    // метод оставлен для совместимости интерфейса ITarget
}

void CWindowTarget::warpPositionSize() {
    auto win = m_window.lock();
    if (!win)
        return;

    // мгновенно применяем текущую goal-геометрию без анимации —
    // используется, например, сразу после X11-подтверждённого
    // ConfigureRequest, где анимация оставила бы "призрачную" тень
    win->setGoalGeometry(win->x(), win->y(), win->w(), win->h(), /*warp=*/true);
}

void CWindowTarget::onUpdateSpace() {
    auto win = m_window.lock();
    if (!win || !space())
        return;

    win->m_workspaceID = space()->workspaceID();
}
