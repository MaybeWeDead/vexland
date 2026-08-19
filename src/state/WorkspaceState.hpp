#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <optional>
#include <algorithm>
#include "../desktop/Workspace.hpp"

// -----------------------------------------------------------------------
// WorkspaceState.hpp — аналог State::CWorkspaceStateTracker. Реестр
// всех живых workspace'ов, тот же принцип безопасности что и
// CWindowState: держим PHLWORKSPACE в основном списке, резолвим наружу
// только через .lock()/find, никогда не отдаём "сырые" ссылки, которые
// могли бы протухнуть.
//
// Упрощено относительно оригинала: нет WorkspaceRuleManager (bound
// monitor per workspace name, persistent workspace priority) — эти
// вещи вернутся вместе с конфиг-парсером, когда будут реальные
// "правила" которые можно на что-то матчить.
// -----------------------------------------------------------------------

class CWorkspaceState {
  public:
    static CWorkspaceState* get() {
        static CWorkspaceState instance;
        return &instance;
    }

    void add(PHLWORKSPACE w) {
        if (!w)
            return;
        m_workspaces.push_back(w);
    }

    void remove(WORKSPACEID id) {
        std::erase_if(m_workspaces, [id](const PHLWORKSPACE& w) { return !w || w->m_id == id; });
    }

    // безопасный lookup по ID
    PHLWORKSPACE byID(WORKSPACEID id) const {
        for (const auto& w : m_workspaces) {
            if (w && w->m_id == id)
                return w;
        }
        return nullptr;
    }

    PHLWORKSPACE byName(const std::string& name) const {
        for (const auto& w : m_workspaces) {
            if (w && w->m_name == name)
                return w;
        }
        return nullptr;
    }

    std::vector<PHLWORKSPACE> all() const {
        std::vector<PHLWORKSPACE> result;
        result.reserve(m_workspaces.size());
        for (const auto& w : m_workspaces) {
            if (w)
                result.push_back(w);
        }
        return result;
    }

    // создаёт workspace и сразу регистрирует его — единая точка входа,
    // чтобы не забыть вызвать add() после create() где-то в другом месте
    PHLWORKSPACE create(WORKSPACEID id, int monitorID, const std::string& name = "") {
        const std::string finalName = name.empty() ? std::to_string(id) : name;
        const bool        special   = isSpecial(id);

        auto ws = CWorkspace::create(id, monitorID, finalName, special);
        if (ws)
            add(ws);

        return ws;
    }

    // аналог isSpecial — раз мы решили ID-схему в Workspace.hpp: любой
    // ID <= WORKSPACE_NAMED_BASE считается специальным (тот же принцип,
    // что и оригинальный -1337 порог)
    bool isSpecial(WORKSPACEID id) const {
        return id <= WORKSPACE_NAMED_BASE;
    }

    // следующий свободный special ID — просто декрементируем от базы,
    // пока не найдём незанятый (упрощённая версия без persistent
    // workspace priority, которой у нас пока нет)
    WORKSPACEID newSpecialID() const {
        WORKSPACEID candidate = WORKSPACE_NAMED_BASE;
        while (byID(candidate) != nullptr)
            candidate--;
        return candidate;
    }

    void rememberWorkspaceForMonitor(const std::string& monitor, WORKSPACEID workspace) {
        m_seenMonitorWorkspaceMap[monitor] = workspace;
    }

    std::optional<WORKSPACEID> rememberedWorkspaceForMonitor(const std::string& monitor) const {
        auto it = m_seenMonitorWorkspaceMap.find(monitor);
        if (it == m_seenMonitorWorkspaceMap.end())
            return std::nullopt;
        return it->second;
    }

    void clear() {
        m_workspaces.clear();
        m_seenMonitorWorkspaceMap.clear();
    }

  private:
    CWorkspaceState()  = default;
    ~CWorkspaceState() = default;

    std::vector<PHLWORKSPACE>                    m_workspaces;
    std::unordered_map<std::string, WORKSPACEID> m_seenMonitorWorkspaceMap;
};
