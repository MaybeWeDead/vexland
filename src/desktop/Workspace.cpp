#include "Workspace.hpp"
#include "view/WindowState.hpp"

#include <algorithm>
#include <cctype>

// -----------------------------------------------------------------------
// Workspace.cpp — аналог CWorkspace.cpp. Основная логика фокус-фолбэка
// и подсчёта окон перенесена по смыслу; полный селектор-язык (r[]/w[]/
// f[]/m[] и т.д.) сознательно упрощён до базовых numeric/name:/special —
// расширим вместе с конфиг-парсером, когда там появится реальный
// потребитель сложных селекторов (window rules, workspace rules).
// -----------------------------------------------------------------------

PHLWORKSPACE CWorkspace::create(WORKSPACEID id, int monitorID, std::string name, bool special) {
    PHLWORKSPACE ws(new CWorkspace(id, monitorID, name, special));
    ws->m_inert = false;

    // ВАЖНО: геометрия монитора ещё не известна на этом этапе (workspace
    // создаётся раньше, чем мы успеваем опросить X-сервер про монитор).
    // Заглушка 1920x1080 — временная; вызывающий код ОБЯЗАН вызвать
    // ws->m_space->setMonitorGeometry(...) сразу после create(), иначе
    // layout будет считать неправильную рабочую область до первого resize.
    ws->m_space = CSpace::create(id, 0, 0, 1920, 1080);

    return ws;
}

CWorkspace::CWorkspace(WORKSPACEID id, int monitorID, std::string name, bool special) :
    m_id(id), m_name(std::move(name)), m_monitorID(monitorID), m_isSpecialWorkspace(special) {}

CWorkspace::~CWorkspace() = default;

// Аналог getLastFocusedWindow() — резолвим через реестр по ID вместо
// хранения WP<CWindow> напрямую; лишний уровень непрямоты того стоит,
// потому что реестр сам гарантирует "если окна больше нет — вернём null"
PHLWINDOW CWorkspace::getLastFocusedWindow() const {
    if (m_lastFocusedWindowID == 0)
        return nullptr;

    auto w = CWindowState::get()->byID(m_lastFocusedWindowID);
    if (!w || !w->m_isMapped || w->m_workspaceID != m_id)
        return nullptr;

    return w;
}

// Аналог getFocusCandidate(): 3-уровневый fallback —
// last focused -> top-left window -> первое найденное окно на workspace
PHLWINDOW CWorkspace::getFocusCandidate() const {
    if (auto w = getLastFocusedWindow())
        return w;

    // top-left: первое окно, чей верхний левый угол ближе всего к (0,0)
    // относительно рабочей области — если несколько кандидатов,
    // упрощённо берём первое найденное в этой позиции
    PHLWINDOW topLeftCandidate;
    for (const auto& w : CWindowState::get()->all()) {
        if (w->m_workspaceID != m_id || !w->m_isMapped)
            continue;

        if (w->x() <= 1 && w->y() <= 1)
            return w; // нашли реально top-left окно, возвращаем сразу

        if (!topLeftCandidate)
            topLeftCandidate = w; // запасной вариант — первое попавшееся
    }

    return topLeftCandidate;
}

// Упрощённая версия matchesStaticSelector: поддерживает числовой ID,
// "name:xxx" и "special"/"specialname" префикс. Полный властный
// селектор-язык (r[]/w[]/f[]/m[] флаги) добавим вместе с конфигом,
// когда появится реальный потребитель этой сложности.
bool CWorkspace::matchesStaticSelector(const std::string& selector) const {
    std::string trimmed = selector;
    // trim пробелов по краям — без завязки на hyprutils string helpers
    while (!trimmed.empty() && std::isspace((unsigned char)trimmed.front()))
        trimmed.erase(trimmed.begin());
    while (!trimmed.empty() && std::isspace((unsigned char)trimmed.back()))
        trimmed.pop_back();

    if (trimmed.empty())
        return true;

    // числовой селектор — прямое сравнение ID
    bool isNumeric = !trimmed.empty() && std::ranges::all_of(trimmed, [](unsigned char c) { return std::isdigit(c) || c == '-'; });
    if (isNumeric) {
        try {
            return std::stoll(trimmed) == m_id;
        } catch (...) {
            return false;
        }
    }

    if (trimmed.starts_with("name:"))
        return m_name == trimmed.substr(5);

    if (trimmed.starts_with("special"))
        return m_isSpecialWorkspace && (trimmed == "special" || m_name == trimmed);

    return false;
}

// Аналог getWindowCount с упрощёнными фильтрами (без onlyPinned/onlyVisible
// раздельно — считаем просто все мапнутые окна этого workspace; фильтры
// добавим когда появится реальный consumer типа монitor overview)
int CWorkspace::getWindowCount() const {
    int count = 0;
    for (const auto& w : CWindowState::get()->all()) {
        if (w->m_workspaceID == m_id && w->m_isMapped)
            count++;
    }
    return count;
}

bool CWorkspace::hasUrgentWindow() const {
    // TODO: Window.hpp пока не хранит m_isUrgent — добавим вместе с
    // urgency hint handling (ICCCM WM_HINTS urgency bit)
    return false;
}

void CWorkspace::rename(const std::string& name) {
    if (m_isSpecialWorkspace)
        return;

    m_name = name;
}

void CWorkspace::changeID(WORKSPACEID id) {
    if (m_id <= 0)
        return; // невалидный переход, аналог оригинальной проверки

    m_id = id;
}

// Аналог updateWindows(): в оригинале это триггерит re-apply правил
// (RULE_PROP_ON_WORKSPACE) для каждого окна. У нас пока нет rule-системы,
// так что метод — просто recalc layout через m_space, что реально нужно
// прямо сейчас (например после add/remove окна).
void CWorkspace::updateWindows() {
    if (m_space)
        m_space->recalculate();
}
