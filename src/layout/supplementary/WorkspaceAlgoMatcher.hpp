#pragma once

#include <map>
#include <string>
#include <functional>
#include <memory>
#include "../algorithm/TiledAlgorithm.hpp"
#include "../algorithm/Algorithm.hpp"
#include "../../config/ConfigManager.hpp"

// -----------------------------------------------------------------------
// WorkspaceAlgoMatcher.hpp — аналог Layout::Supplementary::
// CWorkspaceAlgoMatcher. Фабричный реестр: разные layout-алгоритмы
// (dwindle сейчас, master/monocle/scrolling в будущем) регистрируются
// по имени с factory-функцией. Layout для конкретного workspace
// выбирается через general.layout в ConfigManager (аналог general:layout
// в оригинале), с возможностью расширить per-workspace override позже,
// когда появятся workspace rules в конфиге.
//
// Floating-алгоритмы у нас пока нет отдельной реализации (floating
// окна двигаются напрямую через Space::moveTarget) — оставлена
// структура под будущее расширение, m_floatingAlgos пока не используется.
// -----------------------------------------------------------------------

class CWorkspaceAlgoMatcher {
  public:
    static CWorkspaceAlgoMatcher* get() {
        static CWorkspaceAlgoMatcher instance;
        return &instance;
    }

    bool registerTiledAlgo(const std::string& name, std::function<std::shared_ptr<ITiledAlgorithm>()> factory) {
        if (m_tiledAlgos.contains(name))
            return false;

        m_tiledAlgos[name] = std::move(factory);
        return true;
    }

    bool unregisterAlgo(const std::string& name) {
        return m_tiledAlgos.erase(name) > 0;
    }

    std::shared_ptr<ITiledAlgorithm> createAlgorithmByName(const std::string& name) const {
        auto it = m_tiledAlgos.find(name);
        if (it != m_tiledAlgos.end())
            return it->second();

        if (!m_defaultAlgoName.empty()) {
            auto defIt = m_tiledAlgos.find(m_defaultAlgoName);
            if (defIt != m_tiledAlgos.end())
                return defIt->second();
        }

        return nullptr;
    }

    // выбирает layout для workspace через general.layout в конфиге —
    // аналог tiledAlgoForWorkspace() из оригинала, упрощённый (нет ещё
    // per-workspace rule override, только глобальный дефолт)
    std::string layoutNameForWorkspace(int workspaceID) const {
        (void)workspaceID; // TODO: подключить per-workspace override когда
                            // появятся workspace rules

        if (auto* cfg = CConfigManager::get())
            return cfg->getString("general.layout", m_defaultAlgoName);

        return m_defaultAlgoName;
    }

    std::shared_ptr<ITiledAlgorithm> createAlgorithmForWorkspace(int workspaceID) const {
        return createAlgorithmByName(layoutNameForWorkspace(workspaceID));
    }

    // Аналог updateWorkspaceLayouts() — проходит по всем живым
    // workspace'ам и обновляет их Space::setAlgorithm(...), если
    // конфиг general.layout сменился после reload(). Вызывается
    // из ConfigManager после успешной перезагрузки конфига.
    //
    // ВНИМАНИЕ: требует доступа к CWorkspaceState::get()->all() —
    // инклуд не добавлен здесь намеренно, чтобы избежать циклической
    // зависимости (WorkspaceState.hpp не должен знать про layout/).
    // Вызывающий код (CCompositor или ConfigManager reload handler)
    // должен сам пройтись по CWorkspaceState::get()->all() и вызвать
    // updateAlgoForWorkspace(ws) для каждого — см. TODO в main.cpp.
    template <typename TWorkspace>
    void updateAlgoForWorkspace(std::shared_ptr<TWorkspace> ws) const {
        if (!ws || !ws->m_space)
            return;

        const auto wantedName  = layoutNameForWorkspace(ws->m_id);
        const auto currentAlgo = ws->m_space->algorithm();

        if (currentAlgo && currentAlgo->layoutName() == wantedName)
            return; // уже тот самый алгоритм, менять не нужно

        ws->m_space->setAlgorithm(std::dynamic_pointer_cast<CDwindleAlgorithm>(createAlgorithmByName(wantedName)));
    }

    void setDefaultAlgo(const std::string& name) {
        m_defaultAlgoName = name;
    }

    const std::string& defaultAlgoName() const {
        return m_defaultAlgoName;
    }

  private:
    CWorkspaceAlgoMatcher() {
        registerTiledAlgo("dwindle", [] { return std::make_shared<CDwindleAlgorithm>(); });
        m_defaultAlgoName = "dwindle";
    }
    ~CWorkspaceAlgoMatcher() = default;

    std::map<std::string, std::function<std::shared_ptr<ITiledAlgorithm>()>> m_tiledAlgos;
    std::string                                                              m_defaultAlgoName;
};
