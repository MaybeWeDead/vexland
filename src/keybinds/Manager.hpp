#pragma once

#include <vector>
#include <memory>
#include <string>
#include <string_view>
#include <optional>

extern "C" {
#include <xkbcommon/xkbcommon.h>
}

#include "Bind.hpp"
#include "Key.hpp"

// -----------------------------------------------------------------------
// Manager.hpp — аналог Keybinds::CKeybindManager. Точка входа для
// обработки X11 KeyPress/KeyRelease событий: резолвит физический
// keycode в keysym через xkb_state, ищет совпадающий CBind, вызывает.
//
// Осознанно НЕ перенесено (пока): long-press/repeat scheduling (нужен
// event loop timer, которого пока нет полноценно), shadow binds
// (подавление конфликтов между chord-биндами — неприменимо без chord),
// mouse/axis events (это отдельная тема, InputManager для mouse),
// device-specific handling (multi-keyboard support).
//
// Перенесено: VT switching (Ctrl+Alt+F1-F12 — полезная системная
// фича, не завязана на сложную инфраструктуру), submap tracking,
// централизованный xkb_state для layout-aware резолвинга клавиш.
// -----------------------------------------------------------------------

class CKeybindManager {
  public:
    static CKeybindManager* get() {
        static CKeybindManager instance;
        return &instance;
    }

    ~CKeybindManager();

    // регистрирует новый бинд, возвращает индекс для последующего removeBind
    size_t addBind(CBind&& bind);
    bool   removeBind(size_t index);
    void   clearBinds();

    // точка входа из event loop (main.cpp) — вызывается на каждый
    // XCB_KEY_PRESS/XCB_KEY_RELEASE. keycode — физический X11 keycode
    // (event->detail из xcb_key_press_event_t), modMask — текущая
    // маска модификаторов (из event->state, замаппленная в наш
    // Input::ModifierMask).
    bool onKeyEvent(xcb_keycode_t keycode, Input::ModifierMask modMask, bool pressed);

    // инициализация/обновление xkb translation state — вызывается
    // один раз при старте и на MappingNotify (смена раскладки)
    bool initXkb(xcb_connection_t* conn);
    void updateXkbState(xcb_connection_t* conn);

    std::string_view currentSubmap() const {
        return m_currentSubmap;
    }
    void setSubmap(const std::string& submap) {
        m_currentSubmap = submap;
    }

  private:
    CKeybindManager()  = default;

    // резолвит физический keycode в keysym через текущий xkb_state —
    // это то самое место, где учитывается раскладка клавиатуры
    SResolvedKey resolveKey(xcb_keycode_t keycode) const;

    // VT switching — Ctrl+Alt+F1..F12. Возвращает true, если событие
    // было обработано как VT-переключение (и дальше искать bind не нужно)
    bool handleVT(xkb_keysym_t keysym, Input::ModifierMask modMask) const;

    std::vector<CBind> m_binds;
    std::string         m_currentSubmap;

    xkb_context*        m_xkbContext = nullptr;
    xkb_keymap*         m_xkbKeymap  = nullptr;
    xkb_state*           m_xkbState   = nullptr;
    int32_t              m_xkbDeviceID = -1;
};
