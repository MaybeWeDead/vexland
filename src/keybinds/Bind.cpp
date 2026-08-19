#include "Bind.hpp"

// -----------------------------------------------------------------------
// Bind.cpp — синхронизировано с Key.hpp: сравнение клавиши идёт через
// CKey::matches(SResolvedKey), не строковое. Модификаторы сравниваются
// как битовая маска (Input::ModifierMask), что и корректно, и быстрее
// строкового сравнения.
// -----------------------------------------------------------------------

std::optional<CBind> CBind::make(Input::ModifierMask modMask, const std::string& keyName, BindFlags flags, BindCallback callback, SBindMetadata metadata) {
    if (!callback)
        return std::nullopt; // аналог "Bad callback" из оригинала

    CKey key(keyName);
    if (!key.valid())
        return std::nullopt; // аналог "Unknown key: {}"

    return CBind(modMask, std::move(key), flags, std::move(callback), std::move(metadata));
}

// Аналог matchesContext(): проверяет submap и general applicability
// ДО сравнения самой клавиши. BIND_FLAG_SUBMAP_UNIVERSAL пропускает
// submap-проверку — используется для глобальных биндов типа "выйти
// из любого submap по Escape".
bool CBind::matchesContext(const SBindEventContext& ctx) const {
    if (!m_enabled)
        return false;

    if (!(m_flags & BIND_FLAG_SUBMAP_UNIVERSAL) && m_metadata.submap != ctx.currentSubmap)
        return false;

    const bool wantsRelease = m_flags & BIND_FLAG_RELEASE;
    if (wantsRelease && ctx.pressed)
        return false;
    if (!wantsRelease && !ctx.pressed)
        return false;

    return true;
}

// Аналог matches(): полное сравнение keysym/keycode + модификаторов.
// IGNORE_MODS пропускает проверку модификаторов.
bool CBind::matches(const SBindEventContext& ctx) const {
    if (!matchesContext(ctx))
        return false;

    if (!m_key.matches(ctx.trigger))
        return false;

    if (m_flags & BIND_FLAG_IGNORE_MODS)
        return true;

    return m_modmask == ctx.modifiersNow;
}

SBindResult CBind::invoke() const {
    if (!m_callback)
        return {.success = false, .error = "no callback registered"};

    return m_callback();
}
