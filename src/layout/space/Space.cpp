#include "Space.hpp"
#include "../algorithm/Algorithm.hpp"
#include "../../config/ConfigManager.hpp"

#include <cstdio>
#include <print>
#include <algorithm>

// -----------------------------------------------------------------------
// Space.cpp — аналог Layout::CSpace.cpp. Основная логика (add/remove/
// move/swap делегируются в CAlgorithm) перенесена по смыслу. Главное
// отличие: recheckWorkArea() читает gaps_out через CConfigManager
// вместо CConfigValue<> (нашего аналога системы конфигов у нас нет,
// зато теперь есть реальный ConfigManager — используем его).
//
// Опущено: per-workspace gaps override (WorkspaceRuleManager), float_gaps
// отдельно от tiled gaps, EventBus-подписка на monitor.layoutChanged —
// вернём эти вещи по мере появления реальных потребителей.
// -----------------------------------------------------------------------

std::shared_ptr<CSpace> CSpace::create(int workspaceID, double monX, double monY, double monW, double monH) {
    return std::shared_ptr<CSpace>(new CSpace(workspaceID, monX, monY, monW, monH));
}

CSpace::CSpace(int workspaceID, double monX, double monY, double monW, double monH) :
    m_workspaceID(workspaceID), m_monitorX(monX), m_monitorY(monY), m_monitorW(monW), m_monitorH(monH) {
    recheckWorkArea();
}

void CSpace::add(std::shared_ptr<ITarget> t) {
    if (!t)
        return;

    m_targets.emplace_back(t);
    recheckWorkArea();

    if (m_algorithm)
        m_algorithm->newTarget(t);
}

void CSpace::remove(std::shared_ptr<ITarget> t) {
    if (!t)
        return;

    std::erase_if(m_targets, [&t](const auto& w) {
        auto locked = w.lock();
        return !locked || locked == t;
    });

    recheckWorkArea();

    if (m_algorithm)
        m_algorithm->removeTarget(t);
}

void CSpace::move(std::shared_ptr<ITarget> t, std::optional<SVec2> focalPoint) {
    if (!t)
        return;

    m_targets.emplace_back(t);
    recheckWorkArea();

    if (m_algorithm)
        m_algorithm->movedTarget(t, focalPoint);
}

void CSpace::swap(std::shared_ptr<ITarget> a, std::shared_ptr<ITarget> b) {
    for (auto& w : m_targets) {
        auto locked = w.lock();
        if (locked == a)
            w = b;
        else if (locked == b)
            w = a;
    }

    if (m_algorithm)
        m_algorithm->swapTargets(a, b);
}

void CSpace::setAlgorithm(std::shared_ptr<CDwindleAlgorithm> algo) {
    m_algorithm = algo;

    // ВАЖНО: сразу привязываем алгоритм к этому Space, иначе
    // CDwindleAlgorithm::recalculate() не будет знать геометрию
    // (см. TODO в Algorithm.cpp — geometry берётся через m_space.lock())
    if (m_algorithm)
        m_algorithm->attachSpace(weak_from_this());
}

// Аналог recheckWorkArea: пересчитываем рабочую область (монитор минус
// gaps_out). Читаем gaps_out из ConfigManager с дефолтом на случай,
// если конфиг ещё не загружен или ключ не задан — recheckWorkArea
// НИКОГДА не должен упасть из-за отсутствующего конфига.
void CSpace::recheckWorkArea() {
    double gapsOutValue = m_gapsOut;

    if (auto* cfg = CConfigManager::get())
        gapsOutValue = cfg->getFloat("general.gaps_out", m_gapsOut);

    m_gapsOut = gapsOutValue;

    m_workArea.x = m_monitorX + m_gapsOut;
    m_workArea.y = m_monitorY + m_gapsOut;
    m_workArea.w = std::max(1.0, m_monitorW - 2 * m_gapsOut);
    m_workArea.h = std::max(1.0, m_monitorH - 2 * m_gapsOut);
}

void CSpace::recalculate(eRecalculateReason reason) {
    recheckWorkArea();

    // gapsIn читается самим алгоритмом изнутри при необходимости —
    // тут просто гарантируем что workArea свежая ДО вызова
    // m_algorithm->recalculate(reason), который через m_space.lock()
    // возьмёт актуальную geometry (см. Algorithm.cpp)
    if (m_algorithm)
        m_algorithm->recalculate(reason);
}

void CSpace::toggleTargetFloating(std::shared_ptr<ITarget> t) {
    if (!t)
        return;

    t->setWasTiling(true);
    t->setFloating(!t->floating());
    t->setWasTiling(false);

    recalculate();
}

void CSpace::resizeTarget(const SVec2& delta, std::shared_ptr<ITarget> target) {
    // TODO: интерактивный ресайз — требует InputManager (mouse drag
    // tracking), которого у нас пока нет. Заглушка оставлена в API,
    // чтобы ITarget-контракт был полным уже сейчас.
    (void)delta;
    (void)target;
}

void CSpace::moveTarget(const SVec2& delta, std::shared_ptr<ITarget> target) {
    // аналог: только для floating-таргетов, тайловые двигаются через
    // recalculate() всего дерева, а не точечным moveTarget
    if (!target || !target->floating())
        return;

    auto win = target->window();
    if (!win)
        return;

    win->setGoalGeometry(win->x() + delta.x, win->y() + delta.y, win->w(), win->h());
}

void CSpace::setTargetGeom(const SBox& box, std::shared_ptr<ITarget> target) {
    if (!target || !target->floating())
        return;

    auto win = target->window();
    if (!win)
        return;

    win->setGoalGeometry(box.x, box.y, box.w, box.h);
}
