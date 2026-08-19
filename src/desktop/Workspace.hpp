#pragma once

#include <string>
#include <optional>
#include <memory>
#include <vector>
#include "view/WindowState.hpp"
#include "../layout/space/Space.hpp"

// -----------------------------------------------------------------------
// Workspace.hpp — аналог CWorkspace. Владеет ровно одним CSpace (как и
// в оригинале — 1 workspace = 1 layout-область). ID-схема сохранена:
// положительные ID — обычные нумерованные workspace'ы, отрицательные
// (начиная с условного WORKSPACE_NAMED_BASE) — именованные.
//
// Animated render-offset для свайп-переходов между workspace'ами и
// alpha-fade оставлены как простые числа (см. паттерн из Window.hpp) —
// анимирует их тот же AnimationManager, не отдельная система.
// -----------------------------------------------------------------------

using WORKSPACEID = int64_t;
constexpr WORKSPACEID WORKSPACE_INVALID     = -1;
constexpr WORKSPACEID WORKSPACE_NAMED_BASE  = -1337;

class CWorkspace;
using PHLWORKSPACE = std::shared_ptr<CWorkspace>;
using WPWORKSPACE  = std::weak_ptr<CWorkspace>;

class CWorkspace : public std::enable_shared_from_this<CWorkspace> {
  public:
    static PHLWORKSPACE create(WORKSPACEID id, int monitorID, std::string name, bool special = false);

    ~CWorkspace();

    WORKSPACEID m_id   = WORKSPACE_INVALID;
    std::string m_name;
    int         m_monitorID = -1;

    std::shared_ptr<CSpace> m_space;

    // render-offset для свайпа между workspace'ами (X/Y дельта, куда
    // все окна текущего workspace смещены относительно экрана) —
    // интерполируется AnimationManager'ом, как и geometry окон
    double m_renderOffsetX = 0, m_renderOffsetY = 0;
    double m_alpha = 1.0;

    bool m_visible             = false;
    bool m_isSpecialWorkspace  = false; // scratchpad-подобный workspace
    bool m_wasCreatedEmpty     = true;
    bool m_persistent          = false;

    uint64_t m_lastFocusedWindowID = 0; // 0 = нет; ID-based, не прямой shared_ptr

    bool        inert() const {
        return m_inert;
    }
    void        markInert() {
        m_inert = true;
    }

    int         monitorID() const {
        return m_monitorID;
    }

    // возвращает последнее сфокусированное окно этого workspace, если
    // оно ещё существует и валидно — резолвится через реестр окон,
    // а не хранится напрямую (см. WindowState.hpp паттерн)
    PHLWINDOW getLastFocusedWindow() const;

    // возвращает разумного кандидата на фокус, если getLastFocusedWindow
    // не даёт валидный результат — берёт первое найденное окно на
    // workspace через реестр
    PHLWINDOW getFocusCandidate() const;

    std::string getConfigName() const {
        return m_name.empty() ? std::to_string(m_id) : m_name;
    }

    // простая проверка селектора вида "name:foo" или числового ID —
    // полноценный матчинг (regex и т.д.) добавим вместе с конфигом
    bool matchesStaticSelector(const std::string& selector) const;

    int  getWindowCount() const;
    bool hasUrgentWindow() const;

    void rename(const std::string& name);
    void changeID(WORKSPACEID id);

    void setPersistent(bool p) {
        m_persistent = p;
    }
    bool isPersistent() const {
        return m_persistent;
    }

    void updateWindows(); // пересчитывает layout через m_space

  private:
    CWorkspace(WORKSPACEID id, int monitorID, std::string name, bool special);

    bool m_inert = true;
};

inline bool valid(const PHLWORKSPACE& ref) {
    return ref && !ref->inert();
}
