#include "Target.hpp"
#include "../space/Space.hpp"

#include <cstdio>
#include <cmath>
#include <print>

// -----------------------------------------------------------------------
// Target.cpp — аналог ITarget.cpp из Hyprland. Логика assignToSpace/swap
// сохранена почти 1:1 (это чистая топология, не завязана на Wayland),
// просто CScopeGuard заменён на явный лямбда-паттерн (RAII-гварды из
// hyprutils у нас нет — не тащим лишнюю зависимость ради одного класса).
// -----------------------------------------------------------------------

void ITarget::setPositionGlobal(const STargetBox& box, uint8_t) {
    m_box = box;
    // округление до целых пикселей — избегаем субпиксельного дрожания
    m_box.logicalBox.x = std::round(m_box.logicalBox.x);
    m_box.logicalBox.y = std::round(m_box.logicalBox.y);
    m_box.logicalBox.w = std::round(m_box.logicalBox.w);
    m_box.logicalBox.h = std::round(m_box.logicalBox.h);
}

void ITarget::setPositionGlobal(const SBox& box, uint8_t flags) {
    setPositionGlobal(STargetBox{.logicalBox = box}, flags);
}

void ITarget::assignToSpace(std::shared_ptr<CSpace> space, std::optional<SVec2> focalPoint) {
    if (m_space == space && !m_ghostSpace)
        return;

    const bool hadSpace = !!m_space;

    if (m_space && !m_ghostSpace)
        m_space->remove(shared_from_this());

    m_space = space;

    if (space && hadSpace)
        space->move(shared_from_this(), focalPoint);
    else if (space)
        space->add(shared_from_this());

    if (!space)
        std::println(stderr, "[ WARN ] ITarget: assignToSpace with a null space?");

    m_ghostSpace = false;

    onUpdateSpace();
}

std::shared_ptr<CSpace> ITarget::space() const {
    return m_space;
}

SBox ITarget::position() const {
    return m_box.logicalBox;
}

void ITarget::rememberFloatingSize(const SVec2& size) {
    m_floatingSize = size;
}

SVec2 ITarget::lastFloatingSize() const {
    return m_floatingSize;
}

void ITarget::recalc() {
    setPositionGlobal(m_box);
}

void ITarget::setPseudo(bool x) {
    if (m_pseudo == x)
        return;

    m_pseudo = x;
    recalc();
}

bool ITarget::isPseudo() const {
    return m_pseudo;
}

void ITarget::setPseudoSize(const SVec2& size) {
    m_pseudoSize = size;
    recalc();
}

SVec2 ITarget::pseudoSize() {
    return m_pseudoSize;
}

// Аналог ITarget::swap — меняет местами два таргета в их пространствах
// (может быть в одном Space, может в разных, включая floating-статус).
// Логика сохранена как в оригинале: сначала считаем floating-состояния
// ДО свапа, потом свапаем сами Space-привязки, потом восстанавливаем
// floating (потому что после физического свапа местами это состояние
// должно принадлежать НОВОМУ владельцу таргета).
void ITarget::swap(std::shared_ptr<ITarget> b) {
    if (!b)
        return;

    const bool isFloatingA = floating();
    const bool isFloatingB = b->floating();

    // держим Space живыми на время свапа — если один из них временно
    // остаётся без target'ов, он не должен быть удалён посреди операции
    const auto spaceA = space();
    const auto spaceB = b->space();

    if (b->space() == m_space) {
        // оба таргета в одном Space — самый простой случай
        if (m_space)
            m_space->swap(shared_from_this(), b);
    } else {
        if (m_space)
            m_space->swap(shared_from_this(), b);
        if (b->space())
            b->space()->swap(b, shared_from_this());

        std::swap(m_space, b->m_space);
    }

    // восстанавливаем floating-статус для новых владельцев
    b->setFloating(isFloatingA);
    setFloating(isFloatingB);

    b->onUpdateSpace();
    onUpdateSpace();

    if (m_space)
        m_space->recalculate();
    if (b->space())
        b->space()->recalculate();
}

bool ITarget::wasTiling() const {
    return m_wasTiling;
}

void ITarget::setWasTiling(bool x) {
    m_wasTiling = x;
}
