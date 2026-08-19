#pragma once

extern "C" {
#include <xkbcommon/xkbcommon.h>
}

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// -----------------------------------------------------------------------
// Key.hpp — аналог Keybinds::CKey, использует libxkbcommon (та же
// библиотека, что и оригинал) для layout-aware обработки клавиш —
// это правильный выбор, не завязан на Wayland, полноценно
// поддерживается через xkbcommon-x11 (xkb_x11_get_core_keyboard_device_id,
// xkb_x11_keymap_new_from_device — см. main.cpp инициализацию).
//
// Упрощено относительно оригинала: убран SExternalEventPattern (не
// нужен без plugin/D-Bus event system), убран std::variant KeyEvent
// (у нас нет chord-триггеров, где могло бы понадобиться несколько
// разных типов паттерна для разных позиций в chord'е) — просто
// keysym ИЛИ keycode ИЛИ sided modifier, одно из трёх на CKey.
// -----------------------------------------------------------------------

namespace Input {
    enum eKeyboardModifiers : uint32_t {
        HL_MODIFIER_NONE  = 0,
        HL_MODIFIER_SHIFT = (1 << 0),
        HL_MODIFIER_CTRL  = (1 << 1),
        HL_MODIFIER_ALT   = (1 << 2),
        HL_MODIFIER_META  = (1 << 3), // Super/Windows key
    };

    using ModifierMask = uint32_t;
}

// Резолвнутая клавиша из реального X11-события — то, с чем сравнивается
// CKey::matches(). Заполняется в main.cpp через xkb_state_key_get_one_sym.
struct SResolvedKey {
    xkb_keysym_t                                sym  = 0;
    xkb_keycode_t                                code = 0;
    std::optional<Input::eKeyboardModifiers>     modifier = std::nullopt; // задан, если это событие модификатора самого по себе
};

class CKey {
  public:
    // конструктор из строки — парсит имя клавиши через xkb_keysym_from_name
    // (например "q", "Return", "F1") ИЛИ распознаёт как sided modifier
    // ("SHIFT_L", "CONTROL_R" и т.д.)
    explicit CKey(const std::string& name);

    // конструктор для чистого модификатора без стороны (обычный "Control"
    // из vx.bind("Control", "q", ...) — не привязан к левому/правому)
    explicit CKey(Input::eKeyboardModifiers modifier);

    ~CKey() = default;

    bool matches(const SResolvedKey& key) const;

    bool isMod() const {
        return m_mod.has_value();
    }

    std::optional<xkb_keysym_t> keysym() const {
        return m_sym;
    }
    std::optional<xkb_keycode_t> keycode() const {
        return m_code;
    }
    std::optional<Input::eKeyboardModifiers> modifier() const {
        return m_mod;
    }

    bool valid() const {
        return m_sym.has_value() || m_code.has_value() || m_mod.has_value();
    }

  private:
    std::optional<xkb_keysym_t>              m_sym;
    std::optional<xkb_keycode_t>             m_code;
    std::optional<Input::eKeyboardModifiers> m_mod;
};
