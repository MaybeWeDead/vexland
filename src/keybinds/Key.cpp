#include "Key.hpp"

#include <algorithm>
#include <cctype>

// -----------------------------------------------------------------------
// Key.cpp — парсинг строкового имени клавиши в xkb_keysym_t через
// xkb_keysym_from_name (регистронезависимо), плюс распознавание sided
// modifiers ("SHIFT_L", "CONTROL_R" и т.д.) как отдельный случай перед
// обращением к обычному keysym-парсингу, ровно как это делает оригинал
// (SIDED_MODIFIER_NAMES таблица) — просто без отдельной таблицы, через
// простую проверку суффикса _L/_R на известных именах модификаторов.
// -----------------------------------------------------------------------

namespace {
    // аналог SIDED_MODIFIER_NAMES из оригинала, упрощённый: общий модификатор
    // без привязки к стороне (у нас m_mod не различает left/right — см.
    // TODO в Bind.hpp про ограничение относительно оригинала)
    std::optional<Input::eKeyboardModifiers> modifierFromName(const std::string& upper) {
        if (upper == "SHIFT" || upper == "SHIFT_L" || upper == "SHIFT_R")
            return Input::HL_MODIFIER_SHIFT;
        if (upper == "CONTROL" || upper == "CTRL" || upper == "CONTROL_L" || upper == "CONTROL_R" || upper == "CTRL_L" || upper == "CTRL_R")
            return Input::HL_MODIFIER_CTRL;
        if (upper == "ALT" || upper == "ALT_L" || upper == "ALT_R")
            return Input::HL_MODIFIER_ALT;
        if (upper == "SUPER" || upper == "SUPER_L" || upper == "SUPER_R" || upper == "META" || upper == "MOD4")
            return Input::HL_MODIFIER_META;
        return std::nullopt;
    }
}

CKey::CKey(const std::string& name) {
    std::string upper = name;
    std::ranges::transform(upper, upper.begin(), [](unsigned char c) { return std::toupper(c); });

    // проверяем модификатор ПЕРЕД обычным keysym-парсингом — то же самое
    // условие "modifiers cannot appear after keys" из оригинала имело бы
    // смысл на уровне CBind::make при парсинге списка ключей; здесь,
    // раз у нас один CKey на один слот (mod ИЛИ key, не список),
    // просто определяем тип сразу
    if (auto mod = modifierFromName(upper)) {
        m_mod = mod;
        return;
    }

    // xkb_keysym_from_name регистронезависимо (XKB_KEYSYM_CASE_INSENSITIVE) —
    // так "return", "Return", "RETURN" все распознаются одинаково
    xkb_keysym_t sym = xkb_keysym_from_name(name.c_str(), XKB_KEYSYM_CASE_INSENSITIVE);

    if (sym != XKB_KEY_NoSymbol)
        m_sym = sym;
    // если не распознано — оставляем m_sym/m_code/m_mod все пустыми,
    // valid() вернёт false, CBind::make увидит это и вернёт nullopt
}

CKey::CKey(Input::eKeyboardModifiers modifier) : m_mod(modifier) {}

bool CKey::matches(const SResolvedKey& key) const {
    if (m_mod.has_value()) {
        // сравниваем как модификатор — резолвленный key должен тоже
        // быть событием модификатора с тем же mask
        return key.modifier.has_value() && *key.modifier == *m_mod;
    }

    if (m_sym.has_value())
        return key.sym == *m_sym;

    if (m_code.has_value())
        return key.code == *m_code;

    return false; // невалидный CKey ничему не соответствует
}
