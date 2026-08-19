cmake_minimum_required(VERSION 3.20)
project(vexland CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(PkgConfig REQUIRED)

# --- X11 / XCB стек ---------------------------------------------------
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb)
pkg_check_modules(XCB_RANDR REQUIRED IMPORTED_TARGET xcb-randr)
pkg_check_modules(XCB_XKB REQUIRED IMPORTED_TARGET xcb-xkb)
pkg_check_modules(XKBCOMMON REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(XKBCOMMON_X11 REQUIRED IMPORTED_TARGET xkbcommon-x11)

# --- Lua ---------------------------------------------------------------
# lua.pc НЕ стандартизирован между дистрибутивами (Arch/Debian/Fedora
# называют по-разному) — пробуем несколько известных имён по очереди.
pkg_check_modules(LUA IMPORTED_TARGET lua5.4)
if(NOT LUA_FOUND)
    pkg_check_modules(LUA IMPORTED_TARGET lua-5.4)
endif()
if(NOT LUA_FOUND)
    pkg_check_modules(LUA IMPORTED_TARGET lua)
endif()
if(NOT LUA_FOUND)
    message(FATAL_ERROR "Lua 5.4 not found via pkg-config. On Arch: pacman -S lua")
endif()

add_executable(vexland
    src/main.cpp
    src/Compositor.cpp

    src/desktop/view/Window.cpp
    src/desktop/state/FocusState.cpp
    src/desktop/Workspace.cpp

    src/output/MonitorState.cpp

    src/layout/target/Target.cpp
    src/layout/target/WindowTarget.cpp
    src/layout/space/Space.cpp
    src/layout/algorithm/Algorithm.cpp

    src/config/ConfigManager.cpp

    src/keybinds/Bind.cpp
    src/keybinds/Key.cpp
    src/keybinds/Manager.cpp
)

target_include_directories(vexland PRIVATE src)

target_link_libraries(vexland PRIVATE
    PkgConfig::XCB
    PkgConfig::XCB_RANDR
    PkgConfig::XCB_XKB
    PkgConfig::XKBCOMMON
    PkgConfig::XKBCOMMON_X11
    PkgConfig::LUA
)

target_compile_options(vexland PRIVATE -Wall -Wextra -Wno-unused-parameter)

# TODO: по мере добавления оставшихся файлов (RuleEngine.hpp header-only —
# не требует .cpp; WorkspaceAlgoMatcher.hpp header-only — тоже не требует;
# WorkspaceState.hpp/WindowState.hpp header-only тоже) — список .cpp
# выше должен покрывать всё, но перепроверить при первой сборке, если
# линковщик пожалуется на undefined reference.
