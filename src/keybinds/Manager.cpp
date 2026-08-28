#include "Manager.hpp"

extern "C" {
#include <xkbcommon/xkbcommon-x11.h>
#include <xcb/xcb.h>
}

#include <cstdio>
#include <print>
#include <unistd.h>

// -----------------------------------------------------------------------
// Manager.cpp — xkbcommon-x11 инициализация по паттерну из i3lock
// (единственный найденный референс, решающий ровно нашу задачу —
// X11 keyboard state tracking через XCB, а не Xlib):
//
//   xkb_x11_setup_xkb_extension()       — уведомляем X-сервер что будем
//                                          использовать XKB extension
//   xkb_context_new()                    — создаём xkbcommon context
//   xkb_x11_get_core_keyboard_device_id() — берём device ID клавиатуры
//   xkb_x11_keymap_new_from_device()     — тянем keymap с X-сервера
//   xkb_x11_state_new_from_device()      — создаём state для трекинга
//
// На каждое KeyPress/KeyRelease: xkb_state_key_get_one_sym(state, keycode)
// даёт keysym С УЧЁТОМ текущей раскладки — это и есть layout-aware
// резолвинг, ради которого мы вообще используем xkbcommon вместо
// голого keycode-сравнения.
// -----------------------------------------------------------------------

CKeybindManager::~CKeybindManager() {
    if (m_xkbState)
        xkb_state_unref(m_xkbState);
    if (m_xkbKeymap)
        xkb_keymap_unref(m_xkbKeymap);
    if (m_xkbContext)
        xkb_context_unref(m_xkbContext);
}

bool CKeybindManager::initXkb(xcb_connection_t* conn) {
    uint8_t xkbBaseEvent = 0, xkbBaseError = 0;

    if (xkb_x11_setup_xkb_extension(conn, XKB_X11_MIN_MAJOR_XKB_VERSION, XKB_X11_MIN_MINOR_XKB_VERSION, XKB_X11_SETUP_XKB_EXTENSION_NO_FLAGS, nullptr, nullptr, &xkbBaseEvent,
                                    &xkbBaseError) != 1) {
        std::println(stderr, "[ ERROR ] Could not setup XKB X11 extension");
        return false;
    }

    m_xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!m_xkbContext) {
        std::println(stderr, "[ ERROR ] Could not create xkbcommon context");
        return false;
    }

    m_xkbDeviceID = xkb_x11_get_core_keyboard_device_id(conn);
    if (m_xkbDeviceID < 0) {
        std::println(stderr, "[ ERROR ] Could not get core keyboard device ID");
        return false;
    }

    m_xkbKeymap = xkb_x11_keymap_new_from_device(m_xkbContext, conn, m_xkbDeviceID, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!m_xkbKeymap) {
        std::println(stderr, "[ ERROR ] Could not create xkb keymap from X11 device");
        return false;
    }

    m_xkbState = xkb_x11_state_new_from_device(m_xkbKeymap, conn, m_xkbDeviceID);
    if (!m_xkbState) {
        std::println(stderr, "[ ERROR ] Could not create xkb state from X11 device");
        return false;
    }

    std::println("[ INFO ] xkbcommon-x11 initialized, device id {}", m_xkbDeviceID);
    return true;
}

// Вызывается на XCB_MAPPING_NOTIFY (юзер сменил раскладку через
// setxkbmap/xkb-switch и т.д.) — пересобираем keymap+state с нуля,
// чтобы дальнейшие KeyPress события резолвились правильно.
void CKeybindManager::updateXkbState(xcb_connection_t* conn) {
    if (m_xkbState) {
        xkb_state_unref(m_xkbState);
        m_xkbState = nullptr;
    }
    if (m_xkbKeymap) {
        xkb_keymap_unref(m_xkbKeymap);
        m_xkbKeymap = nullptr;
    }

    m_xkbKeymap = xkb_x11_keymap_new_from_device(m_xkbContext, conn, m_xkbDeviceID, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!m_xkbKeymap) {
        std::println(stderr, "[ ERROR ] Failed to reload xkb keymap after layout change");
        return;
    }

    m_xkbState = xkb_x11_state_new_from_device(m_xkbKeymap, conn, m_xkbDeviceID);
    if (!m_xkbState)
        std::println(stderr, "[ ERROR ] Failed to reload xkb state after layout change");
}

SResolvedKey CKeybindManager::resolveKey(xcb_keycode_t keycode) const {
    SResolvedKey resolved;

    if (!m_xkbState)
        return resolved; // пустой resolved — sym=0, ничему не совпадёт

    resolved.sym  = xkb_state_key_get_one_sym(m_xkbState, keycode);
    resolved.code = keycode;

    return resolved;
}

size_t CKeybindManager::addBind(CBind&& bind) {
    m_binds.push_back(std::move(bind));
    return m_binds.size() - 1;
}

bool CKeybindManager::removeBind(size_t index) {
    if (index >= m_binds.size())
        return false;

    m_binds.erase(m_binds.begin() + index);
    return true;
}

void CKeybindManager::clearBinds() {
    m_binds.clear();
}

// VT switching — Ctrl+Alt+F1..F12. Это системная фича через ioctl на
// /dev/tty (VT_ACTIVATE), не через X11 напрямую. Здесь только детекция
// правильной комбинации, реальный ioctl-вызов — TODO, потому что
// требует прав на /dev/tty* и открытого файлового дескриптора текущего
// tty, которых пока нет в main.cpp.
bool CKeybindManager::handleVT(xkb_keysym_t keysym, Input::ModifierMask modMask) const {
    const bool ctrlAlt = (modMask & Input::HL_MODIFIER_CTRL) && (modMask & Input::HL_MODIFIER_ALT);
    if (!ctrlAlt)
        return false;

    if (keysym >= XKB_KEY_F1 && keysym <= XKB_KEY_F12) {
        int vtNumber = keysym - XKB_KEY_F1 + 1;
        std::println("[ INFO ] VT switch requested to tty{} (not yet implemented — needs ioctl VT_ACTIVATE)", vtNumber);
        return true; // считаем обработанным, чтобы не пытаться искать обычный bind
    }

    return false;
}

bool CKeybindManager::onKeyEvent(xcb_keycode_t keycode, Input::ModifierMask modMask, bool pressed) {
    const SResolvedKey resolved = resolveKey(keycode);

    std::println("[ DEBUG ] onKeyEvent: resolved sym={} code={} mods={}", resolved.sym, resolved.code, modMask);

    if (resolved.sym == XKB_KEY_NoSymbol)
        return false;

    if (pressed && handleVT(resolved.sym, modMask))
        return true;

    SBindEventContext ctx;
    ctx.modifiersNow  = modMask;
    ctx.trigger       = resolved;
    ctx.pressed       = pressed;
    ctx.currentSubmap = m_currentSubmap;

    for (auto& bind : m_binds) {
        if (!bind.matches(ctx))
            continue;

        const auto result = bind.invoke();
        if (!result.success)
            std::println(stderr, "[ WARN ] keybind callback failed: {}", result.error);

        return true; // первый совпавший bind обрабатывает событие, не ищем дальше
    }
    
    std::println("[ DEBUG ] No matching bind found for sym={}", resolved.sym);
    return false;
}

// -----------------------------------------------------------------------
// X11 Passive Grabs (xcb_grab_key)
// -----------------------------------------------------------------------

// Вспомогательная функция для конвертации нашей маски Input::ModifierMask
// в битовые маски модификаторов X11 (XCB_MOD_MASK_*)
static uint16_t modMaskToX11(Input::ModifierMask mods) {
    uint16_t x11 = 0;
    if (mods & Input::HL_MODIFIER_SHIFT) x11 |= XCB_MOD_MASK_SHIFT;
    if (mods & Input::HL_MODIFIER_CTRL)  x11 |= XCB_MOD_MASK_CONTROL;
    if (mods & Input::HL_MODIFIER_ALT)   x11 |= XCB_MOD_MASK_1;
    if (mods & Input::HL_MODIFIER_META)  x11 |= XCB_MOD_MASK_4;
    return x11;
}

void CKeybindManager::setX11Connection(xcb_connection_t* conn, xcb_window_t root) {
    m_x11Conn = conn;
    m_x11Root = root;
    // Захват (grab) будет выполнен позже, когда бинды будут загружены из конфига
}

void CKeybindManager::regrabKeys() {
    if (!m_x11Conn || m_x11Root == XCB_WINDOW_NONE || !m_xkbKeymap)
        return;

    // 1. Снимаем все предыдущие захваты с root-окна
    xcb_ungrab_key(m_x11Conn, XCB_GRAB_ANY, m_x11Root, XCB_MOD_MASK_ANY);

    // 2. Проходим по всем биндам и делаем xcb_grab_key
    for (const auto& bind : m_binds) {
        // Если бинд завязан на модификатор (без конкретной клавиши), пропускаем, 
        // т.к. XCB не позволяет грабить пустой keycode таким образом
        if (!bind.key().keysym().has_value() && !bind.key().keycode().has_value())
            continue;

        xcb_keycode_t keycode = 0;

        if (bind.key().keysym().has_value()) {
            xkb_keysym_t sym = *bind.key().keysym();
            xkb_keysym_t lower_sym = xkb_keysym_to_lower(sym); // На случай, если в конфиге написали заглавную "A" вместо "a"

            // Ищем физический keycode по keysym в загруженном xkb_keymap
            xkb_keycode_t min_kc = xkb_keymap_min_keycode(m_xkbKeymap);
            xkb_keycode_t max_kc = xkb_keymap_max_keycode(m_xkbKeymap);
            
            for (xkb_keycode_t kc = min_kc; kc <= max_kc; ++kc) {
                xkb_layout_index_t num_layouts = xkb_keymap_num_layouts_for_key(m_xkbKeymap, kc);
                for (xkb_layout_index_t l = 0; l < num_layouts; ++l) {
                    const xkb_keysym_t* syms = nullptr;
                    int num_syms = xkb_keymap_key_get_syms_by_level(m_xkbKeymap, kc, l, 0, &syms);
                    if (num_syms > 0 && (syms[0] == sym || syms[0] == lower_sym)) {
                        keycode = kc;
                        break;
                    }
                }
                if (keycode != 0) break;
            }
        } else if (bind.key().keycode().has_value()) {
            keycode = *bind.key().keycode();
        }

        if (keycode == 0) {
            std::println(stderr, "[ WARN ] Failed to find X11 keycode for keybind");
            continue;
        }

        uint16_t x11_mods = modMaskToX11(bind.modMask());

        // В X11 NumLock (Mod2) и CapsLock (Lock) часто мешают грабам. 
        // Если они нажаты, X11 не сматчит бинд. Поэтому мы регистрируем 
        // граб для всех 4 комбинаций: без локов, с NumLock, с CapsLock, с обоими.
        uint16_t extra_mods[] = {0, XCB_MOD_MASK_LOCK, XCB_MOD_MASK_2, XCB_MOD_MASK_LOCK | XCB_MOD_MASK_2};
        for (uint16_t ex : extra_mods) {
            xcb_grab_key(m_x11Conn, 1, m_x11Root, x11_mods | ex, keycode, XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        }
    }

    xcb_flush(m_x11Conn);
    std::println("[ INFO ] X11 passive keybinds grabbed successfully");
}
