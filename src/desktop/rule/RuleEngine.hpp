#pragma once

#include <vector>
#include <memory>
#include <string>
#include "Rule.hpp"

// -----------------------------------------------------------------------
// RuleEngine.hpp — аналог Desktop::Rule::CRuleEngine. Реестр всех
// зарегистрированных window rules + применение их к окнам.
// -----------------------------------------------------------------------

class CRuleEngine {
  public:
    static CRuleEngine* get() {
        static CRuleEngine instance;
        return &instance;
    }

    void registerRule(std::shared_ptr<CWindowRule> rule) {
        if (!rule)
            return;
        m_rules.push_back(std::move(rule));
    }

    void unregisterRule(const std::string& name) {
        std::erase_if(m_rules, [&name](const auto& r) { return r && r->name() == name; });
    }

    void unregisterRule(const std::shared_ptr<CWindowRule>& rule) {
        std::erase(m_rules, rule);
    }

    void clearAllRules() {
        m_rules.clear();
    }

    // применяет все зарегистрированные правила к конкретному окну —
    // вызывается при mapWindow() (первое применение) и при onUpdateMeta
    // (класс/title сменились — правило типа "class:firefox" могло
    // перестать/начать матчиться). Все совпавшие правила применяются
    // по порядку регистрации — более позднее правило может
    // переопределить более раннее для конфликтующих эффектов.
    void applyRulesTo(PHLWINDOW w) const {
        if (!w)
            return;

        for (const auto& rule : m_rules) {
            if (!rule || !rule->matches(w))
                continue;

            for (const auto& effect : rule->effects()) {
                switch (effect.effect) {
                    case RULE_EFFECT_FLOAT: w->m_isFloating = true; break;
                    case RULE_EFFECT_TILE: w->m_isFloating = false; break;
                    case RULE_EFFECT_WORKSPACE:
                        try {
                            w->m_workspaceID = std::stoi(effect.value);
                        } catch (...) {
                            // некорректное значение workspace ID в правиле —
                            // не крашим, просто игнорируем этот конкретный эффект
                        }
                        break;
                    default: break;
                }
            }
        }
    }

    const std::vector<std::shared_ptr<CWindowRule>>& rules() const {
        return m_rules;
    }

  private:
    CRuleEngine()  = default;
    ~CRuleEngine() = default;

    std::vector<std::shared_ptr<CWindowRule>> m_rules;
};
