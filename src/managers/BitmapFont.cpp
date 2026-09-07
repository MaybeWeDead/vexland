#include "BitmapFont.hpp"

#include <unordered_map>
#include <vector>
#include <cstring>

// -----------------------------------------------------------------------
// BitmapFont.cpp — таблица глифов 5x7. Формат классический для этого
// стиля embedded-шрифтов (тот же принцип, что общеизвестный
// "Adafruit GFX 5x7 font" и десятки его клонов в open-source
// firmware/embedded проектах) — 5 столбцов на символ, каждый байт
// представляет один столбец (не строку!), биты снизу вверх.
//
// Мы храним по СТРОКАМ (7 байт на глиф, 5 бит на байт) для простоты
// рисования построчно в rasterizeText() — конвертация из
// столбцового представления в строчное сделана один раз здесь,
// вручную, для покрытия только тех символов, что реально нужны нашим
// splash-фразам (ASCII 0x20-0x7E), не полного набора экзотики.
// -----------------------------------------------------------------------

namespace BitmapFont {

namespace {

// Каждый символ — 7 строк, младшие 5 бит каждого байта = столбцы
// (бит 4 = самый левый пиксель, бит 0 = самый правый).
struct SGlyphRows {
    uint8_t rows[7];
};

// Компактная таблица только тех символов, что реально встречаются в
// splash-фразах: A-Z, a-z, 0-9, пробел, и пунктуация ! ' ( ) , . : ? -
const std::unordered_map<char, SGlyphRows>& glyphTable() {
    static const std::unordered_map<char, SGlyphRows> table = {
        {' ', {{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000}}},
        {'!', {{0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100, 0b00000}}},
        {'\'', {{0b00100, 0b00100, 0b01000, 0b00000, 0b00000, 0b00000, 0b00000}}},
        {'(', {{0b00010, 0b00100, 0b01000, 0b01000, 0b01000, 0b00100, 0b00010}}},
        {')', {{0b01000, 0b00100, 0b00010, 0b00010, 0b00010, 0b00100, 0b01000}}},
        {',', {{0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b00100, 0b01000}}},
        {'-', {{0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000}}},
        {'.', {{0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100}}},
        {':', {{0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000}}},
        {'?', {{0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100}}},

        {'0', {{0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110}}},
        {'1', {{0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}}},
        {'2', {{0b01110, 0b10001, 0b00001, 0b00110, 0b01000, 0b10000, 0b11111}}},
        {'3', {{0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110}}},
        {'4', {{0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010}}},
        {'5', {{0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110}}},
        {'6', {{0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110}}},
        {'7', {{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000}}},
        {'8', {{0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110}}},
        {'9', {{0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100}}},

        {'A', {{0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}}},
        {'B', {{0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110}}},
        {'C', {{0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110}}},
        {'D', {{0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100}}},
        {'E', {{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111}}},
        {'F', {{0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000}}},
        {'G', {{0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111}}},
        {'H', {{0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001}}},
        {'I', {{0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110}}},
        {'J', {{0b00001, 0b00001, 0b00001, 0b00001, 0b10001, 0b10001, 0b01110}}},
        {'K', {{0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001}}},
        {'L', {{0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111}}},
        {'M', {{0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001}}},
        {'N', {{0b10001, 0b11001, 0b10101, 0b10101, 0b10011, 0b10001, 0b10001}}},
        {'O', {{0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}}},
        {'P', {{0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000}}},
        {'Q', {{0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101}}},
        {'R', {{0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001}}},
        {'S', {{0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110}}},
        {'T', {{0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100}}},
        {'U', {{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110}}},
        {'V', {{0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100}}},
        {'W', {{0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010}}},
        {'X', {{0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001}}},
        {'Y', {{0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100}}},
        {'Z', {{0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111}}},
    };
    return table;
}

// Строчные буквы рисуем теми же глифами, что заглавные (уменьшенный
// набор — bitmap-шрифт 5x7 слишком мал для отдельного, визуально
// отличимого lowercase; это стандартный компромисс для таких
// низкоразрешающих шрифтов, тот же подход у большинства 5x7 embedded
// таблиц).
char normalizeChar(char c) {
    if (c >= 'a' && c <= 'z')
        return static_cast<char>(c - 'a' + 'A');
    return c;
}

} // namespace

const uint8_t* glyphFor(char c) {
    static const uint8_t emptyGlyph[7] = {0, 0, 0, 0, 0, 0, 0};

    const auto& table = glyphTable();
    auto it = table.find(normalizeChar(c));
    if (it == table.end())
        return emptyGlyph;

    return it->second.rows;
}

xcb_pixmap_t rasterizeText(xcb_connection_t* conn, xcb_drawable_t drawable, const std::string& text, int scale,
                           int* outWidth, int* outHeight) {
    if (!conn || text.empty() || scale < 1) {
        if (outWidth) *outWidth = 0;
        if (outHeight) *outHeight = 0;
        return XCB_PIXMAP_NONE;
    }

    constexpr int CHAR_SPACING = 1; // пикселей между глифами, до масштабирования

    const int width  = static_cast<int>(text.size()) * (GLYPH_WIDTH + CHAR_SPACING) * scale;
    const int height = GLYPH_HEIGHT * scale;

    if (outWidth) *outWidth = width;
    if (outHeight) *outHeight = height;

    // depth=1 — bitmap mask, не полноцветное изображение. Используется
    // как XRender mask Picture в Splash.cpp, не как готовая картинка.
    xcb_pixmap_t pixmap = xcb_generate_id(conn);
    xcb_create_pixmap(conn, 1, pixmap, drawable, static_cast<uint16_t>(width), static_cast<uint16_t>(height));

    xcb_gcontext_t gc = xcb_generate_id(conn);
    const uint32_t gcValues[] = {1, 0}; // foreground=1 (белый бит), background=0
    xcb_create_gc(conn, gc, pixmap, XCB_GC_FOREGROUND | XCB_GC_BACKGROUND, gcValues);

    // Сначала чистим весь pixmap в 0 (чёрный/прозрачный в mask-семантике)
    {
        const uint32_t clearValues[] = {0};
        xcb_gcontext_t clearGc = xcb_generate_id(conn);
        xcb_create_gc(conn, clearGc, pixmap, XCB_GC_FOREGROUND, clearValues);
        xcb_rectangle_t fullRect{0, 0, static_cast<uint16_t>(width), static_cast<uint16_t>(height)};
        xcb_poly_fill_rectangle(conn, pixmap, clearGc, 1, &fullRect);
        xcb_free_gc(conn, clearGc);
    }

    // Рисуем каждый символ как набор xcb_point_t (по одному вызову
    // xcb_poly_point на глиф — экономим round-trips по сравнению с
    // point-per-request, хотя всё ещё не самый быстрый путь; для
    // splash-текста, рисуемого один раз при старте, это не критично).
    std::vector<xcb_point_t> points;

    for (size_t charIdx = 0; charIdx < text.size(); ++charIdx) {
        const uint8_t* glyph  = glyphFor(text[charIdx]);
        const int      baseX  = static_cast<int>(charIdx) * (GLYPH_WIDTH + CHAR_SPACING) * scale;

        for (int row = 0; row < GLYPH_HEIGHT; ++row) {
            const uint8_t rowBits = glyph[row];

            for (int col = 0; col < GLYPH_WIDTH; ++col) {
                // бит 4 (старший из используемых 5) = самый левый столбец
                const bool pixelOn = (rowBits >> (GLYPH_WIDTH - 1 - col)) & 1;
                if (!pixelOn)
                    continue;

                // scale x scale блок пикселей на один "логический" пиксель шрифта
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        points.push_back(xcb_point_t{
                            static_cast<int16_t>(baseX + col * scale + sx),
                            static_cast<int16_t>(row * scale + sy)
                        });
                    }
                }
            }
        }
    }

    if (!points.empty())
        xcb_poly_point(conn, XCB_COORD_MODE_ORIGIN, pixmap, gc, static_cast<uint32_t>(points.size()), points.data());

    xcb_free_gc(conn, gc);

    return pixmap;
}

} // namespace BitmapFont
