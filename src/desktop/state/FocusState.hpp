#pragma once

#include <memory>
#include <optional>
#include "../desktop/view/Window.hpp"
#include "../output/Monitor.hpp"

// -----------------------------------------------------------------------
// FocusState.hpp — аналог Desktop::focusState(). Централизованное
// "кто сейчас активен" — окно, монитор. Тот же ID/weak-based принцип
// безопасности: не храним PHLWINDOW напрямую как "источник истины",
// а ID + резолвинг через CWindowState, чтобы исчезнувшее окно не
// оставляло dangling-ссылку тут.
//
// Упрощено относительно оригинала: нет отдельного surface-фокуса
// (клавиатурный фокус на конкретном Wayland surface внутри окна —
// у нас в X11-модели это неприменимо, фокус всегда на уровне окна
// целиком через XSetInputFocus), нет FOCUS_REASON enum с детальной
// телеметрией причины смены фокуса (добавим когда появится IPC,
// которому эта телеметрия будет реально нужна для событий).
// -----------------------------------------------------------------------

enum eFocusReason : uint8_t {
    FOCUS_REASON_OTHER = 0,
    FOCUS_REASON_NEW_WINDOW,
    FOCUS_REASON_CLICK,
    FOCUS_REASON_KEYBIND,
    FOCUS_REASON_MONITOR_CHANGE,
};

class CFocusState {
  public:
    static CFocusState* get() {
        static CFocusState instance;
        return &instance;
    }

    // текущее сфокусированное окно — резолвится через реестр, если
    // окно уже уничтожено, безопасно вернёт nullptr вместо мусора
    PHLWINDOW window() const;

    // низкоуровневая установка фокуса — только меняет внутреннее
    // состояние, НЕ трогает X11 (для этого см. fullWindowFocus)
    void setWindowID(uint64_t id) {
        m_focusedWindowID = id;
    }
    void resetWindow() {
        m_focusedWindowID = 0;
    }

    // "полный" фокус — меняет внутреннее состояние И реально
    // устанавливает X11 input focus на окно через xcb_set_input_focus.
    // Это то, что реально должно вызываться из большинства мест кода
    // (map нового окна, alt-tab, клик по окну), а не setWindowID
    // напрямую — так гарантируется, что X-сервер и наше внутреннее
    // состояние никогда не разъезжаются.
    void fullWindowFocus(xcb_connection_t* conn, PHLWINDOW w, eFocusReason reason = FOCUS_REASON_OTHER);

    PHLMONITOR monitor() const {
        return m_monitor;
    }
    void setMonitor(PHLMONITOR m) {
        m_monitor = m;
    }

  private:
    CFocusState()  = default;
    ~CFocusState() = default;

    uint64_t   m_focusedWindowID = 0; // 0 = нет сфокусированного окна
    PHLMONITOR m_monitor;              // текущий "активный" монитор (где курсор/последний фокус)
};
