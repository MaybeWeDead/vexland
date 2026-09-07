#pragma once

#include <string>
#include <cstdint>

extern "C" {
#include <xcb/xcb.h>
}

// -----------------------------------------------------------------------
// BitmapFont.hpp — минимальный 5x7 pixel font для splash-текста (см.
// Splash.cpp). Сознательно НЕ используем Xft/fontconfig — это отдельная
// (Xlib-based, не XCB) библиотека, требующая Xlib/XCB моста
// (libx11-xcb) и добавляющая риск несовместимости API, которого мы уже
// достаточно словили на VXR. Bitmap-шрифт даёт менее красивый
// антиалиасинг, но нулевые доп. зависимости и полный контроль.
//
// Покрытие: ASCII 0x20 (space) - 0x7E (~) — достаточно для английских
// splash-фраз. Всё, что вне диапазона, рисуется как пустой прямоугольник
// (не крашится, просто "дыра" в тексте).
// -----------------------------------------------------------------------

namespace BitmapFont {

constexpr int GLYPH_WIDTH  = 5;
constexpr int GLYPH_HEIGHT = 7;

// Возвращает 7 байт (по одному на строку глифа), каждый байт — 5 младших
// бит используются (столбцы слева направо, бит 0 = самый левый столбец).
const uint8_t* glyphFor(char c);

// Растеризует строку в новый XCB pixmap глубины 1 (bitmap mask) —
// белые пиксели = буквы, чёрные = фон. Используется как alpha mask
// в XRender composite (см. Splash.cpp), не как готовое изображение с
// цветом — цвет задаётся отдельно через solid-fill Picture.
//
// scale — целочисленный множитель размера (1 = 5x7px на символ, 2 =
// 10x14px и т.д.) — bitmap-шрифт в размере 1 нечитаем на большом
// экране, обычно нужен scale 3-4 для комфортного чтения.
//
// Возвращает XCB_PIXMAP_NONE при ошибке (например, conn невалиден).
// Вызывающий код владеет возвращённым pixmap и должен освободить его
// через xcb_free_pixmap, когда он больше не нужен.
xcb_pixmap_t rasterizeText(xcb_connection_t* conn, xcb_drawable_t drawable, const std::string& text, int scale,
                           int* outWidth, int* outHeight);

} // namespace BitmapFont
