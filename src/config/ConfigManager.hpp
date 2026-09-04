#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <chrono>
#include <memory>

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// -----------------------------------------------------------------------
// ConfigManager.hpp v2 — переписано с sol2 на голый lua_State*, по
// архитектурному паттерну Config::Lua::CConfigManager из Hyprland.
// Взято по духу (не скопировано): watchdog против зависших скриптов,
// setFn-регистрация lua_CFunction, fromLuaState() для доступа к C++
// объекту менеджера изнутри C callback'ов (у lua_CFunction нет this).
//
// Осознанно НЕ перенесено (Hyprland-специфичное/пока не нужное нам):
// plugin lua functions, REPL режим, layout providers через Lua,
// window/layer rules merge-логика, device configs (per-input-device
// настройки) — добавим по мере появления реальных потребителей.
// -----------------------------------------------------------------------

struct SKeybind {
    std::string mod;
    std::string key;
    std::string action;
    std::vector<std::string> args;
    int luaCallbackRef = LUA_NOREF; // если action == "lua", храним ссылку на функцию в registry
};

enum eConfigValueType : uint8_t {
    CONFIG_VALUE_FLOAT,
    CONFIG_VALUE_STRING,
    CONFIG_VALUE_BOOL,
};

struct SConfigValue {
    eConfigValueType type;
    double floatVal = 0;
    std::string stringVal;
    bool boolVal = false;
};

class CConfigManager {
  public:
    static CConfigManager* get() {
        static CConfigManager instance;
        return &instance;
    }

    ~CConfigManager();

    // грузит .lua файл. Ошибки (синтаксис, watchdog timeout, runtime
    // exception) ловятся и логируются — конфиг НИКОГДА не крашит WM,
    // при ошибке остаёмся на дефолтных/предыдущих значениях.
    bool load(const std::string& path);

    double      getFloat(const std::string& key, double defaultValue) const;
    std::string getString(const std::string& key, const std::string& defaultValue) const;
    bool        getBool(const std::string& key, bool defaultValue) const;

    const std::vector<SKeybind>& keybinds() const {
        return m_keybinds;
    }

    // список команд из exec_once = "cmd" в .lua — запускаются единожды
    // сразу после старта event loop (см. main.cpp). Аналог exec-once
    // из Hyprland-конфига, только без парсинга "cmd1 & cmd2 & cmd3" —
    // каждый vx.exec_once("...") это отдельная строка/отдельный вызов,
    // никакого shell-разбора амперсандов на нашей стороне (сама команда
    // всё равно идёт через /bin/sh -c, так что "a & b" в ОДНОЙ строке
    // технически тоже сработает, если написать так в конфиге — но это
    // uже забота автора конфига, не наша).
    const std::vector<std::string>& execOnceCommands() const {
        return m_execOnceCommands;
    }

    const std::string& lastError() const {
        return m_lastError;
    }

    // достаём C++ CConfigManager* из lua_State* — нужно внутри
    // lua_CFunction callback'ов, у которых нет захвата this. Работает
    // через lua registry: при инициализации кладём указатель на себя
    // под специальный ключ, тут его читаем обратно.
    static CConfigManager* fromLuaState(lua_State* L);

    // вызывается из C-callback'ов регистрации (vx_set/vx_bind) — не
    // приватные, потому что должны быть доступны свободным функциям
    // в .cpp, которые выступают как lua_CFunction
    void internalSet(const std::string& key, SConfigValue value);
    void internalBind(const std::string& mod, const std::string& key, const std::string& action, std::vector<std::string> args, int luaRef = LUA_NOREF);
    void internalExecOnce(const std::string& cmd) {
        m_execOnceCommands.push_back(cmd);
    }

    static constexpr int WATCHDOG_INSTRUCTION_INTERVAL = 10000; // проверяем таймаут каждые N инструкций Lua VM
    static constexpr int TIMEOUT_CONFIG_LOAD_MS         = 1500; // максимум времени на загрузку всего конфига

  private:
    CConfigManager();

    void        registerAPI();
    static void watchdogHook(lua_State* L, lua_Debug* ar);

    lua_State*  m_lua = nullptr;

    std::unordered_map<std::string, SConfigValue> m_values;
    std::vector<SKeybind>                          m_keybinds;
    std::vector<std::string>                       m_execOnceCommands;
    std::string                                    m_lastError;

    std::chrono::steady_clock::time_point m_watchdogDeadline;
    bool                                   m_watchdogTripped = false;
};
