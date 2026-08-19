# Vexland — чекпоинт архива (финальный на конец сессии)

Статус на момент архивации. Это НЕ готовый к сборке проект — часть инклудов
между файлами может не совпадать 1:1 из-за путей, плюс есть открытые TODO.
Список ниже — что реально написано и что предстоит дальше.

## Готово (34 файла)

- **Compositor.hpp/.cpp** — точка сборки всего проекта, initManagers()
  в 3 стадии (PRIORITY -> BASICINIT -> LATE), реальный X11 connect,
  getVTNr()/bumpNofile() перенесены из оригинала почти дословно
- **main.cpp** — event loop skeleton: XCB connect, error handler,
  MapRequest/UnmapNotify/DestroyNotify, picom auto-spawn как child-процесс
  с supervise/restart. ЕЩЁ НЕ СВЯЗАН с Compositor.cpp/CKeybindManager —
  это следующий шаг при продолжении работы
- **desktop/view/Window.hpp/.cpp** — окно на голом xcb_window_t, geometry
  через setGoalGeometry()/applyCurrentGeometryToX11(), alpha через
  _NET_WM_WINDOW_OPACITY (picom подхватывает fade)
- **desktop/view/WindowState.hpp** — реестр окон, безопасный lookup
- **desktop/Workspace.hpp/.cpp** — workspace с CSpace, упрощённый selector
- **desktop/state/FocusState.hpp/.cpp** — кто активен, РЕАЛЬНЫЙ
  xcb_set_input_focus (не заглушка)
- **state/WorkspaceState.hpp** — реестр workspace'ов
- **output/Monitor.hpp, MonitorState.hpp/.cpp** — РЕАЛЬНЫЙ опрос через
  XRandR (get_screen_resources_current -> get_output_info -> get_crtc_info),
  не заглушка 1920x1080
- **layout/** (target/, space/, algorithm/, LayoutManager.hpp) — полный
  layout-стек, dwindle-тайлинг, читает gaps из ConfigManager
- **config/ConfigManager.hpp/.cpp** — Lua на голом lua_State* (не sol2),
  watchdog против зависших конфигов, vx.set()/vx.bind() API
- **keybinds/** (Bind, Key, Manager) — CKey через настоящий xkbcommon-x11
  (паттерн из референса i3lock), CBind сравнение через keysym, Manager
  с реальной xkb_x11_* инициализацией

## Известные несостыковки после архивации

Инклуды использовали относительные пути под структуру ВО ВРЕМЯ написания
каждого файла (разные подпапки /home/claude). После сведения в единую
src/ иерархию часть путей нужно перепроверить перед первой попыткой
компиляции — особенно #include "../../desktop/..." style пути.

## Критичный следующий шаг (при продолжении)

**main.cpp НЕ переписан под финальную архитектуру** — по договорённости
в разговоре, main.cpp трогаем последним, один раз, когда все системы
готовы. На момент архивации main.cpp содержит только раннюю версию
(до Compositor.hpp/ConfigManager v2/keybinds) с базовым XCB event loop.

Что нужно сделать в main.cpp при продолжении:
1. Заменить самодельный initX11()/eventLoop() на CCompositor::get()->init(configPath)
2. Добавить обработку XCB_KEY_PRESS/XCB_KEY_RELEASE -> CKeybindManager::onKeyEvent()
3. Добавить XCB_MAPPING_NOTIFY -> CKeybindManager::updateXkbState()
4. При MapRequest: CWindow::create() -> CWindowState::add() ->
   CLayoutManager::newTarget() (через CWindowTarget::create() обёртку) ->
   CFocusState::fullWindowFocus()
5. Связать vx.bind() данные из ConfigManager с реальными CBind
   регистрациями в CKeybindManager (сейчас ConfigManager хранит
   SKeybind структуры, но никто их не конвертирует в CBind)

## TODO / дальнейшие шаги (по приоритету, после main.cpp)

1. InputManager — mouse tracking для drag/resize (ButtonPress/MotionNotify)
2. keybinds VT switching — handleVT() определяет комбинацию, но реальный
   ioctl(VT_ACTIVATE) ещё не написан (нужны права на /dev/tty*)
3. CWorkspace::create() — убрать заглушку геометрии, использовать
   CMonitorState::get()->primary()/byName()
4. Window rules, group/swallow/wobble — заготовки полей в Window.hpp есть,
   логики нет
5. LayoutManager не связан с WorkspaceState — регистрация CSpace в
   CLayoutManager нужно прописать явно при CWorkspace::create()

## Архитектурные решения, зафиксированные в разговоре

- Название проекта: **Vexland** (не Hypr-производное, во избежание
  конфликта с брендом Hyprland)
- picom "вшит" в WM как child-процесс (fork+exec в main.cpp), не
  отдельная зависимость которую юзер должен сам стартовать
- Lua-конфиги как в Hyprland 0.55+/AwesomeWM, но на голом lua_State*
  (не sol2) — по духу паттернов реального Hyprland config/lua/
- group/swallow используются через ID + реестр, не прямые shared_ptr
- Анимации визуальных эффектов (blur/shadow/rounding) — забота picom,
  не WM; WM только двигает геометрию через xcb_configure_window
- Есть черновой crash-screen текст ("oopsie daisy") по мотивам
  Hyprland's crashed lockscreen fallback — чисто для колорита, не
  реализован технически

## Полезные внешние референсы, найденные в процессе

- **xkbcommon-x11 полный рабочий пример**: исходники i3lock (единственный
  найденный проект, решающий XKB+XCB задачу ровно как нам нужно, не Wayland)
- **XRandR через XCB паттерн**: исходники libkscreen (KDE)
