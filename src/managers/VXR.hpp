#pragma once

extern "C" {
#include <xcb/xcb.h>
}

#include "../desktop/view/Window.hpp"
#include <unordered_map>

// -----------------------------------------------------------------------
// VXR.hpp — минимальный XRender-based compositor. Никакого picom, никакого
// GLX/OpenGL. REDIRECT_MANUAL: сервер ПЕРЕСТАЁТ сам рисовать redirected
// окна на экран — с этого момента ТОЛЬКО мы решаем, что показывается.
// Каждый repaint() композитит ВСЕ окна (не только прозрачные) в их
// реальном stacking order на overlay window.
//
// ВАЖНО, почему не REDIRECT_AUTOMATIC (было в первой версии, не
// сработало): в AUTOMATIC сервер продолжает рисовать окно НАПРЯМУЮ на
// экран параллельно с тем, что мы рисуем на overlay. Результат —
// непрозрачный оригинал снизу перекрывает любую "прозрачность", которую
// мы пытаемся нарисовать сверху. MANUAL убирает оригинал из показа
// вообще, оставляя это полностью на нашей ответственности.
//
// Осознанно НЕ входит в v1: blur, shadows, XDAMAGE-based partial
// redraw (перерисовываем весь стек на каждый tick — дороже по CPU, чем
// damage-tracking, но на порядок проще и достаточно для нескольких
// терминалов; если станет узким местом — damage-tracking это
// отдельный, изолированный доп. заход поверх этой структуры).
//
// Требует: xcb-composite, xcb-render, xcb-xfixes (проверь на сборке —
// libxcb-composite0-dev, libxcb-render0-dev, libxcb-xfixes0-dev).
// -----------------------------------------------------------------------

namespace VXR {

// Инициализация: проверяет наличие COMPOSITE и RENDER extensions,
// получает overlay window через xcb_composite_get_overlay_window.
// Вызывается один раз при старте (см. Compositor::init). Если любая
// extension отсутствует — VXR тихо отключается (isAvailable() вернёт
// false), Vexland продолжает работать без прозрачности, как раньше.
bool init(xcb_connection_t* conn, xcb_window_t root);

bool isAvailable();

// Redirect + создание Picture для окна — вызывается при map каждого
// окна (см. main.cpp::onMapRequest). Без этого окно не участвует в
// compositing вообще (рисуется сервером напрямую, без прозрачности —
// безопасный fallback, а не крах).
void registerWindow(xcb_connection_t* conn, PHLWINDOW w);

// Снимает redirect и освобождает Picture/Pixmap для окна — вызывается
// при уничтожении окна (см. main.cpp::onDestroyNotify). Без этого —
// утечка X11-ресурсов на сервере (pixmap'ы не освобождаются сами).
void unregisterWindow(xcb_connection_t* conn, PHLWINDOW w);

// Выставляет целевую opacity окна (0.0 = полностью прозрачно, 1.0 =
// непрозрачно). Не анимирует само по себе — вызывающий код (например,
// FocusState при смене активного окна) должен сам решить, дёргать ли
// это напрямую или через AnimationManager (см. TODO в .cpp — на v1
// делаем без анимации самой opacity, только моментальный переход,
// чтобы не usложнять AnimationManager раньше времени).
void setWindowOpacity(xcb_connection_t* conn, PHLWINDOW w, float opacity);

// Перерисовывает весь composited кадр — вызывается на каждый tick
// event loop, ПОСЛЕ AnimationManager::tick() (порядок важен: сначала
// геометрия обновляется, потом мы рисуем актуальный кадр). В отличие
// от первой (AUTOMATIC) версии, теперь рисует ВСЕ redirected окна
// каждый кадр, не только прозрачные — потому что сервер их сам больше
// не рисует вообще (REDIRECT_MANUAL). Throttled внутри до ~60 FPS.
void repaint(xcb_connection_t* conn, xcb_window_t root);

} // namespace VXR
