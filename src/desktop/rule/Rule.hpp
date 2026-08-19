#pragma once

#include <string>
#include <unordered_map>
#include <optional>
#include <regex>
#include <cstdint>
#include <type_traits>

#include "../view/Window.hpp"

// -----------------------------------------------------------------------
// Rule.hpp — аналог Desktop::Rule::IRule. Упрощено относительно
// оригинала: убрана exec-token система (правила, привязанные к
// конкретному запущенному через spawn процессу — требует TokenManager,
// которого у нас нет), убран layer-rule тип (у нас пока нет layer
// surfaces концепции вообще, только обычные окна).
//
// Матчинг: вместо кастомного IMatchEngine с отдельными реализациями
// используем std::regex напрямую — для class/title regex матчинга
// этого достаточно, не нужен отдельный слой абстракции на нашем
// уровне сложности.
// -----------------------------------------------------------------------

enum eRuleProperty : uint32_t {
    RULE_PROP_NONE    = 0,
    RULE_PROP_CLASS   = (1 << 0),
    RULE_PROP_TITLE   = (1 << 1),
    RULE_PROP_FLOATING = (1 << 2),
};

enum eRuleEffect : uint8_t {
    RULE_EFFECT_NONE = 0,
    RULE_EFFECT_FLOAT,      // окно всегда floating
    RULE_EFFECT_TILE,       // окно всегда tiled
    RULE_EFFECT_WORKSPACE,  // окно открывается на конкретном workspace (значение = workspace ID как строка)
};

struct SRuleEffectEntry {
    eRuleEffect effect;
    std::string value; // для RULE_EFFECT_WORKSPACE — ID воркспейса; для остальных не используется
};

class CWindowRule {
  public:
    explicit CWindowRule(const std::string& name = "") : m_name(name) {}

    // регистрирует условие матчинга — например registerMatch(RULE_PROP_CLASS, "firefox")
    void registerMatch(eRuleProperty prop, const std::string& pattern) {
        try {
            m_matchers[prop] = std::regex(pattern, std::regex::icase);
            m_mask |= prop;
        } catch (const std::regex_error& e) {
            // некорректный regex в правиле — не крашим WM, просто это
            // правило никогда не сматчится (mask не выставлен для prop)
        }
    }

    void addEffect(eRuleEffect effect, const std::string& value = "") {
        m_effects.push_back({effect, value});
    }

    void setEnabled(bool x) {
        m_enabled = x;
    }
    bool isEnabled() const {
        return m_enabled;
    }

    const std::string& name() const {
        return m_name;
    }

    const std::vector<SRuleEffectEntry>& effects() const {
        return m_effects;
    }

    // проверяет, матчится ли это правило на конкретное окно — все
    // зарегистрированные условия должны совпасть (AND логика, как
    // в оригинале через getPropertiesMask + has())
    bool matches(PHLWINDOW w) const {
        if (!m_enabled || !w)
            return false;

        if (m_mask == 0)
            return false; // правило без единого условия не матчит ничего

        if (m_mask & RULE_PROP_CLASS) {
            auto it = m_matchers.find(RULE_PROP_CLASS);
            if (it == m_matchers.end() || !std::regex_match(w->m_class, it->second))
                return false;
        }

        if (m_mask & RULE_PROP_TITLE) {
            auto it = m_matchers.find(RULE_PROP_TITLE);
            if (it == m_matchers.end() || !std::regex_match(w->m_title, it->second))
                return false;
        }

        return true;
    }

  private:
    std::string                                    m_name;
    bool                                            m_enabled = true;
    std::underlying_type_t<eRuleProperty>          m_mask = 0;
    std::unordered_map<eRuleProperty, std::regex>  m_matchers;
    std::vector<SRuleEffectEntry>                  m_effects;
};
