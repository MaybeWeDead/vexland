#include "ConfigManager.hpp"

#include <cstdio>
#include <print>
#include <cstring>

// -----------------------------------------------------------------------
// ConfigManager.cpp v2 — реализация на голом Lua C API. Ключевые
// механизмы, по духу как у Hyprland:
//
// 1. fromLuaState() — кладём указатель на CConfigManager в lua registry
//    под уникальный light userdata ключ при инициализации, читаем
//    обратно внутри lua_CFunction callback'ов (у которых нет this).
//
// 2. watchdogHook() — через lua_sethook с LUA_MASKCOUNT прерывает
//    выполнение, если конфиг завис (например `while true do end`),
//    вызывая luaL_error — это гарантирует, что кривой конфиг не
//    подвесит WM насмерть.
// -----------------------------------------------------------------------

static const char* CONFIG_MGR_REGISTRY_KEY = "vexland.config_manager_ptr";

CConfigManager::CConfigManager() {
    m_lua = luaL_newstate();
    luaL_openlibs(m_lua); // TODO: сузить до безопасного подмножества библиотек
                          // (base/math/string/table), когда конфиг станет
                          // достаточно стабильным чтобы думать про sandboxing;
                          // сейчас open всех либ упрощает отладку

    // кладём указатель на себя в registry, чтобы fromLuaState() мог его достать
    lua_pushlightuserdata(m_lua, (void*)this);
    lua_setfield(m_lua, LUA_REGISTRYINDEX, CONFIG_MGR_REGISTRY_KEY);

    registerAPI();
}

CConfigManager::~CConfigManager() {
    if (m_lua)
        lua_close(m_lua);
}

CConfigManager* CConfigManager::fromLuaState(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, CONFIG_MGR_REGISTRY_KEY);
    auto* mgr = static_cast<CConfigManager*>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return mgr;
}

// Вызывается каждые WATCHDOG_INSTRUCTION_INTERVAL инструкций Lua VM.
// Если с момента старта загрузки конфига прошло больше отведённого
// времени — прерываем выполнение через luaL_error (это кидает Lua
// error, который поймает pcall в load()).
void CConfigManager::watchdogHook(lua_State* L, lua_Debug* /*ar*/) {
    auto* mgr = fromLuaState(L);
    if (!mgr)
        return;

    const auto now = std::chrono::steady_clock::now();
    if (now < mgr->m_watchdogDeadline)
        return;

    mgr->m_watchdogTripped = true;
    luaL_error(L, "config execution timed out (possible infinite loop)");
}

// --- lua_CFunction callbacks для vx.set/vx.bind ---
// Свободные функции (не методы класса), потому что lua_CFunction — это
// obычный C-указатель на функцию без захвата контекста. Достаём нужный
// CConfigManager* через fromLuaState() внутри каждой.

static int lua_vx_set(lua_State* L) {
    auto* mgr = CConfigManager::fromLuaState(L);
    if (!mgr)
        return 0;

    const char* key = luaL_checkstring(L, 1);

    SConfigValue value;
    int          valueType = lua_type(L, 2);

    if (valueType == LUA_TNUMBER) {
        value.type     = CONFIG_VALUE_FLOAT;
        value.floatVal = lua_tonumber(L, 2);
    } else if (valueType == LUA_TBOOLEAN) {
        value.type    = CONFIG_VALUE_BOOL;
        value.boolVal = lua_toboolean(L, 2);
    } else if (valueType == LUA_TSTRING) {
        value.type      = CONFIG_VALUE_STRING;
        value.stringVal = lua_tostring(L, 2);
    } else {
        // неподдерживаемый тип — не крашим Lua VM, просто предупреждаем
        // и молча игнорируем присвоение (та же философия "конфиг не
        // должен убивать WM", распространённая и на отдельные вызовы)
        std::println(stderr, "[ CONFIG WARN ] vx.set(\"{}\", ...): unsupported value type", key);
        return 0;
    }

    mgr->internalSet(key, value);
    return 0;
}

static int lua_vx_bind(lua_State* L) {
    auto* mgr = CConfigManager::fromLuaState(L);
    if (!mgr)
        return 0;

    const char* mod    = luaL_checkstring(L, 1);
    const char* key    = luaL_checkstring(L, 2);
    const char* action = luaL_checkstring(L, 3);

    std::vector<std::string> args;
    int                      nargs = lua_gettop(L);

    // если 4-й аргумент — функция, регистрируем её как Lua callback
    // через luaL_ref (аналог того, как Hyprland хранит колбэки для
    // keybind'ов вида vx.bind(mod, key, "lua", function() ... end))
    int luaRef = LUA_NOREF;
    if (nargs >= 4 && lua_type(L, 4) == LUA_TFUNCTION) {
        lua_pushvalue(L, 4);
        luaRef = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        for (int i = 4; i <= nargs; ++i) {
            if (lua_type(L, i) == LUA_TSTRING)
                args.emplace_back(lua_tostring(L, i));
            else if (lua_type(L, i) == LUA_TNUMBER)
                args.emplace_back(std::to_string(lua_tonumber(L, i)));
        }
    }

    mgr->internalBind(mod, key, action, std::move(args), luaRef);
    return 0;
}

void CConfigManager::registerAPI() {
    lua_newtable(m_lua); // создаём таблицу "vx"

    lua_pushcfunction(m_lua, lua_vx_set);
    lua_setfield(m_lua, -2, "set");

    lua_pushcfunction(m_lua, lua_vx_bind);
    lua_setfield(m_lua, -2, "bind");

    lua_setglobal(m_lua, "vx"); // vx = { set = ..., bind = ... }
}

void CConfigManager::internalSet(const std::string& key, SConfigValue value) {
    m_values[key] = std::move(value);
}

void CConfigManager::internalBind(const std::string& mod, const std::string& key, const std::string& action, std::vector<std::string> args, int luaRef) {
    SKeybind kb;
    kb.mod            = mod;
    kb.key            = key;
    kb.action         = action;
    kb.args           = std::move(args);
    kb.luaCallbackRef = luaRef;

    m_keybinds.push_back(std::move(kb));
}

bool CConfigManager::load(const std::string& path) {
    m_lastError.clear();
    m_watchdogTripped = false;

    // выставляем дедлайн ДО начала загрузки — watchdog хук сравнивает
    // с этим временем на каждые WATCHDOG_INSTRUCTION_INTERVAL инструкций
    m_watchdogDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TIMEOUT_CONFIG_LOAD_MS);

    lua_sethook(m_lua, watchdogHook, LUA_MASKCOUNT, WATCHDOG_INSTRUCTION_INTERVAL);

    int loadResult = luaL_loadfile(m_lua, path.c_str());
    if (loadResult != LUA_OK) {
        m_lastError = lua_tostring(m_lua, -1);
        lua_pop(m_lua, 1);
        lua_sethook(m_lua, nullptr, 0, 0); // снимаем hook, раз выполнение не началось
        std::println(stderr, "[ CONFIG ERROR ] Failed to parse {}: {}", path, m_lastError);
        return false;
    }

    int callResult = lua_pcall(m_lua, 0, 0, 0);

    // снимаем watchdog hook сразу после выполнения — не хотим, чтобы
    // он продолжал тикать при будущих несвязанных lua_pcall вызовах
    // (например, при вызове keybind callback'ов из InputManager)
    lua_sethook(m_lua, nullptr, 0, 0);

    if (callResult != LUA_OK) {
        m_lastError = lua_tostring(m_lua, -1);
        lua_pop(m_lua, 1);
        std::println(stderr, "[ CONFIG ERROR ] Failed to run {}: {}", path, m_lastError);
        return false;
    }

    return true;
}

double CConfigManager::getFloat(const std::string& key, double defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end() || it->second.type != CONFIG_VALUE_FLOAT)
        return defaultValue;
    return it->second.floatVal;
}

std::string CConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end() || it->second.type != CONFIG_VALUE_STRING)
        return defaultValue;
    return it->second.stringVal;
}

bool CConfigManager::getBool(const std::string& key, bool defaultValue) const {
    auto it = m_values.find(key);
    if (it == m_values.end() || it->second.type != CONFIG_VALUE_BOOL)
        return defaultValue;
    return it->second.boolVal;
}
