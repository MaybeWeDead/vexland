#pragma once

#include <string>
#include <vector>
#include <functional>
#include <optional>
#include <type_traits>
#include <cstdint>

#include "Key.hpp"

// -----------------------------------------------------------------------
// Bind.hpp — X11-версия CBind. Сохранены флаги и общая философия
// (bind = ключи + условия + callback), но убраны: chord-последовательности
// (несколько отдельных нажатий подряд), per-device биндинги (HID
// device targeting), click/drag отдельно от обычных keybind'ов —
// это всё вернём отдельными слоями, когда появится реальный
// InputManager с полноценным event tracking.
//
// Оставлено из практических соображений: RELEASE/REPEAT/LOCKED —
// самые часто используемые флаги, без них даже базовый набор
// keybind'ов (volume up на repeat, submap на release) не работает.
//
// Теперь хранит CKey вместо строк — синхронизировано с Key.hpp,
// сравнение идёт через настоящий keysym/keycode (layout-aware),
// а не строковое сравнение имён.
// -----------------------------------------------------------------------

enum eBindFlags : uint16_t {
    BIND_FLAG_LOCKED           = (1 << 0), // работает даже когда экран заблокирован
    BIND_FLAG_RELEASE          = (1 << 1), // срабатывает на отпускание, не на нажатие
    BIND_FLAG_REPEAT           = (1 << 2), // повторяется, пока клавиша удерживается
    BIND_FLAG_IGNORE_MODS      = (1 << 3), // не проверяет точное совпадение модификаторов
    BIND_FLAG_SUBMAP_UNIVERSAL = (1 << 4), // работает во всех submap'ах, не только текущем
};

using BindFlags = std::underlying_type_t<eBindFlags>;

struct SBindMetadata {
    std::string                displayKey;   // человекочитаемое представление (для будущего UI/help overlay)
    std::optional<std::string> description;
    std::string                submap;       // в каком submap'е активен этот бинд ("" = global)
    std::string                submapReset;  // если задан — после срабатывания сбрасывает submap на этот
};

struct SBindResult {
    bool        success = true;
    std::string error;
};

using BindCallback = std::function<SBindResult()>;

// Контекст события для проверки совпадения — что нажато сейчас,
// какие модификаторы активны. Упрощённая версия без held-keys-списка
// (нет chord'ов, поэтому не нужно отслеживать несколько зажатых клавиш)
struct SBindEventContext {
    Input::ModifierMask modifiersNow = Input::HL_MODIFIER_NONE; // текущая маска модификаторов
    SResolvedKey         trigger;                                // резолвнутая клавиша-триггер
    bool                  pressed = true;                        // true = press event, false = release event
    std::string           currentSubmap;                         // текущий активный submap на момент события
};

class CBind {
  public:
    static std::optional<CBind> make(Input::ModifierMask modMask, const std::string& keyName, BindFlags flags, BindCallback callback, SBindMetadata metadata = {});

    CBind(CBind&&) noexcept            = default;
    CBind& operator=(CBind&&) noexcept = default;
    CBind(const CBind&)                = delete;
    CBind& operator=(const CBind&)     = delete;

    // проверяет совпадение с контекстом события — учитывает submap,
    // press/release направление, точность модификаторов
    bool matches(const SBindEventContext& ctx) const;

    SBindResult invoke() const;

    bool enabled() const {
        return m_enabled;
    }
    void setEnabled(bool x) {
        m_enabled = x;
    }

    bool hasFlag(eBindFlags flag) const {
        return m_flags & flag;
    }
    BindFlags flags() const {
        return m_flags;
    }

    Input::ModifierMask modMask() const {
        return m_modmask;
    }
    const CKey& key() const {
        return m_key;
    }
    const SBindMetadata& metadata() const {
        return m_metadata;
    }

  private:
    CBind(Input::ModifierMask modMask, CKey key, BindFlags flags, BindCallback callback, SBindMetadata metadata) :
        m_callback(std::move(callback)), m_flags(flags), m_modmask(modMask), m_key(std::move(key)), m_metadata(std::move(metadata)) {}

    bool matchesContext(const SBindEventContext& ctx) const;

    BindCallback         m_callback;
    BindFlags            m_flags = 0;
    Input::ModifierMask  m_modmask = Input::HL_MODIFIER_NONE;
    CKey                 m_key;
    SBindMetadata         m_metadata;
    bool                  m_enabled = true;
};
