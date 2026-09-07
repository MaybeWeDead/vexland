#include "VXR.hpp"
#include "../config/ConfigManager.hpp"
#include "../desktop/view/WindowState.hpp"
#include "BitmapFont.hpp"
#include "Splash.hpp"

extern "C" {
#include <xcb/composite.h>
#include <xcb/render.h>
#include <xcb/xfixes.h>
#include <xcb/shape.h>
}

#include <print>
#include <cstring>
#include <algorithm>
#include <vector>
#include <cmath>
#include <chrono>
#include <unordered_map>

// -----------------------------------------------------------------------
// VXR.cpp v2 — REDIRECT_MANUAL. См. VXR.hpp для архитектурного обзора и
// объяснения, почему v1 (AUTOMATIC) не могло дать настоящую прозрачность.
//
// Ключевое отличие от v1: сервер БОЛЬШЕ НЕ рисует redirected окна сам.
// Каждый repaint() обязан нарисовать ВЕСЬ экран заново — фон + все окна
// в правильном stacking order — иначе экран останется чёрным (то самое
// поведение, что мы видели в первых попытках v1, когда overlay был
// полноэкранным, но пустым).
// -----------------------------------------------------------------------

namespace VXR {

namespace {

bool g_available = false;

xcb_window_t             g_overlayWindow = XCB_WINDOW_NONE;
xcb_render_pictformat_t  g_overlayFormat = 0;
xcb_render_pictformat_t  g_formatARGB32  = 0;
xcb_render_pictformat_t  g_formatRGB24   = 0;

struct SWindowRenderState {
    xcb_pixmap_t          pixmap    = XCB_PIXMAP_NONE;
    xcb_render_picture_t  picture   = XCB_NONE;
    xcb_render_picture_t  alphaMask = XCB_NONE;
    float                 opacity   = 1.0f;
    bool                  redirected = false;
};

std::unordered_map<uint64_t, SWindowRenderState> g_windows;

xcb_render_pictformat_t findFormatForDepth(xcb_connection_t* conn, uint8_t depth) {
    auto cookie = xcb_render_query_pict_formats(conn);
    auto reply  = xcb_render_query_pict_formats_reply(conn, cookie, nullptr);
    if (!reply)
        return 0;

    xcb_render_pictformat_t result = 0;

    auto* formats   = xcb_render_query_pict_formats_formats(reply);
    const int count = xcb_render_query_pict_formats_formats_length(reply);

    for (int i = 0; i < count; ++i) {
        if (formats[i].type == XCB_RENDER_PICT_TYPE_DIRECT && formats[i].depth == depth) {
            result = formats[i].id;
            break;
        }
    }

    free(reply);
    return result;
}

xcb_render_pictformat_t formatForWindowDepth(uint8_t depth) {
    if (depth == 32)
        return g_formatARGB32;
    if (depth == 24)
        return g_formatRGB24;
    return 0;
}

// Строит набор прямоугольников для скруглённого "речевого пузыря" под
// splash-текстом — тот же staircase-подход, что WindowShape.cpp
// использует для скругления окон, только тут это не Shape-маска на
// реальном окне, а обычные закрашенные прямоугольники, которые мы сами
// композитим (пузырь — не окно, у него нет X11 border/bounding shape).
std::vector<xcb_rectangle_t> buildRoundedBubbleRects(int width, int height, int radius) {
    std::vector<xcb_rectangle_t> rects;

    if (radius <= 0 || width <= 0 || height <= 0) {
        rects.push_back(xcb_rectangle_t{0, 0, static_cast<uint16_t>(std::max(width, 0)), static_cast<uint16_t>(std::max(height, 0))});
        return rects;
    }

    radius = std::min(radius, std::min(width, height) / 2);
    constexpr int STEPS = 4;

    if (height > 2 * radius)
        rects.push_back(xcb_rectangle_t{0, static_cast<int16_t>(radius), static_cast<uint16_t>(width), static_cast<uint16_t>(height - 2 * radius)});

    for (int step = 0; step < STEPS; ++step) {
        const double yTop = (static_cast<double>(step) / STEPS) * radius;
        const double yBot = (static_cast<double>(step + 1) / STEPS) * radius;

        auto insetAt = [radius](double y) -> int {
            const double dy = radius - y;
            const double underSqrt = static_cast<double>(radius) * radius - dy * dy;
            if (underSqrt <= 0.0)
                return radius;
            return radius - static_cast<int>(std::floor(std::sqrt(underSqrt)));
        };

        const int inset = std::max(insetAt(yTop), insetAt(yBot));

        const int16_t  stripY      = static_cast<int16_t>(std::floor(yTop));
        const int16_t  stripYBot   = static_cast<int16_t>(std::ceil(yBot));
        const uint16_t stripHeight = static_cast<uint16_t>(std::max(1, stripYBot - stripY));

        if (width - 2 * inset > 0) {
            rects.push_back(xcb_rectangle_t{static_cast<int16_t>(inset), stripY, static_cast<uint16_t>(width - 2 * inset), stripHeight});
            const int16_t bottomStripY = static_cast<int16_t>(height - stripY - stripHeight);
            rects.push_back(xcb_rectangle_t{static_cast<int16_t>(inset), bottomStripY, static_cast<uint16_t>(width - 2 * inset), stripHeight});
        }
    }

    return rects;
}

// Ищет root pixmap обоев через _XROOTPMAP_ID (стандартная договорённость
// между wallpaper-сеттерами вроде feh/nitrogen/xwallpaper и compositor'ами
// — тот же механизм использует picom). ESETROOT_PMAP_ID — легаси-дубликат
// того же атома для очень старых сеттеров, проверяем на случай если
// _XROOTPMAP_ID почему-то не выставлен, а старый есть.
//
// Возвращает XCB_PIXMAP_NONE, если атом не найден/невалиден — вызывающий
// код должен использовать сплошную заливку как fallback (лучше видимый
// цвет, чем краш или мусорные пиксели).
xcb_pixmap_t findRootBackgroundPixmap(xcb_connection_t* conn, xcb_window_t root) {
    // Atom ID кэшируем — он не меняется в течение всей X11-сессии.
    // Сам PIXMAP ID за атомом НЕ кэшируем — он обновляется каждый раз,
    // когда пользователь меняет обои, и мы хотим это подхватывать без
    // перезапуска Vexland.
    static xcb_atom_t cachedRootPmapAtom    = XCB_ATOM_NONE;
    static xcb_atom_t cachedEsetrootAtom    = XCB_ATOM_NONE;
    static bool        atomsResolved         = false;

    if (!atomsResolved) {
        auto resolve = [&](const char* name) -> xcb_atom_t {
            auto cookie = xcb_intern_atom(conn, 1, strlen(name), name);
            auto reply  = xcb_intern_atom_reply(conn, cookie, nullptr);
            if (!reply)
                return XCB_ATOM_NONE;
            const xcb_atom_t atom = reply->atom;
            free(reply);
            return atom;
        };

        cachedRootPmapAtom = resolve("_XROOTPMAP_ID");
        cachedEsetrootAtom = resolve("ESETROOT_PMAP_ID");
        atomsResolved       = true;
    }

    auto tryAtom = [&](xcb_atom_t atom) -> xcb_pixmap_t {
        if (atom == XCB_ATOM_NONE)
            return XCB_PIXMAP_NONE;

        auto propCookie = xcb_get_property(conn, 0, root, atom, XCB_ATOM_PIXMAP, 0, 1);
        auto propReply  = xcb_get_property_reply(conn, propCookie, nullptr);
        if (!propReply)
            return XCB_PIXMAP_NONE;

        xcb_pixmap_t result = XCB_PIXMAP_NONE;
        if (propReply->type == XCB_ATOM_PIXMAP && propReply->format == 32 &&
            xcb_get_property_value_length(propReply) >= static_cast<int>(sizeof(xcb_pixmap_t))) {
            result = *static_cast<xcb_pixmap_t*>(xcb_get_property_value(propReply));
        }

        free(propReply);
        return result;
    };

    if (xcb_pixmap_t p = tryAtom(cachedRootPmapAtom); p != XCB_PIXMAP_NONE)
        return p;

    return tryAtom(cachedEsetrootAtom);
}

// Обновляет backing pixmap+Picture окна перед отрисовкой этого кадра.
// Возвращает false, если окно нельзя отрисовать в этом кадре (исчезло,
// необычная глубина и т.п.) — вызывающий код должен просто пропустить
// его, не считать это фатальной ошибкой всего repaint().
bool refreshWindowPicture(xcb_connection_t* conn, PHLWINDOW w, SWindowRenderState& state) {
    if (state.pixmap != XCB_PIXMAP_NONE) {
        xcb_free_pixmap(conn, state.pixmap);
        state.pixmap = XCB_PIXMAP_NONE;
    }

    state.pixmap = xcb_generate_id(conn);
    xcb_composite_name_window_pixmap(conn, w->getX11Window(), state.pixmap);

    auto geomCookie = xcb_get_geometry(conn, w->getX11Window());
    auto geomReply  = xcb_get_geometry_reply(conn, geomCookie, nullptr);
    if (!geomReply)
        return false;

    const xcb_render_pictformat_t windowFormat = formatForWindowDepth(geomReply->depth);
    free(geomReply);

    if (!windowFormat)
        return false;

    if (state.picture != XCB_NONE)
        xcb_render_free_picture(conn, state.picture);

    state.picture = xcb_generate_id(conn);
    xcb_render_create_picture(conn, state.picture, state.pixmap, windowFormat, 0, nullptr);

    return true;
}

} // namespace

bool init(xcb_connection_t* conn, xcb_window_t root) {
    if (!conn) {
        g_available = false;
        return false;
    }

    const auto* compositeExt = xcb_get_extension_data(conn, &xcb_composite_id);
    const auto* renderExt    = xcb_get_extension_data(conn, &xcb_render_id);
    const auto* xfixesExt    = xcb_get_extension_data(conn, &xcb_xfixes_id);

    if (!compositeExt || !compositeExt->present || !renderExt || !renderExt->present ||
        !xfixesExt || !xfixesExt->present) {
        std::println(stderr, "[ WARN ] VXR: COMPOSITE, RENDER or XFIXES extension missing — transparency disabled");
        g_available = false;
        return false;
    }

    {
        auto verCookie = xcb_xfixes_query_version(conn, 5, 0);
        auto verReply  = xcb_xfixes_query_version_reply(conn, verCookie, nullptr);
        if (!verReply) {
            std::println(stderr, "[ WARN ] VXR: xcb_xfixes_query_version failed — transparency disabled");
            g_available = false;
            return false;
        }
        free(verReply);
    }

    g_formatARGB32 = findFormatForDepth(conn, 32);
    g_formatRGB24  = findFormatForDepth(conn, 24);

    if (!g_formatARGB32 || !g_formatRGB24) {
        std::println(stderr, "[ WARN ] VXR: could not find required XRender pict formats — transparency disabled");
        g_available = false;
        return false;
    }

    auto cookie = xcb_composite_get_overlay_window(conn, root);
    auto reply  = xcb_composite_get_overlay_window_reply(conn, cookie, nullptr);
    if (!reply) {
        std::println(stderr, "[ WARN ] VXR: xcb_composite_get_overlay_window failed — transparency disabled");
        g_available = false;
        return false;
    }

    g_overlayWindow = reply->overlay_win;
    free(reply);

    {
        auto geomCookie = xcb_get_geometry(conn, g_overlayWindow);
        auto geomReply  = xcb_get_geometry_reply(conn, geomCookie, nullptr);
        if (!geomReply) {
            std::println(stderr, "[ WARN ] VXR: xcb_get_geometry on overlay window failed — transparency disabled");
            g_available = false;
            return false;
        }

        g_overlayFormat = formatForWindowDepth(geomReply->depth);
        std::println("[ INFO ] VXR: overlay window depth={} format={}", geomReply->depth, g_overlayFormat);
        free(geomReply);

        if (!g_overlayFormat) {
            std::println(stderr, "[ WARN ] VXR: no matching pict format for overlay depth — transparency disabled");
            g_available = false;
            return false;
        }
    }

    // Overlay должно пропускать клики насквозь всегда — input shape
    // пустой один раз при инициализации, НЕ трогаем его больше (в
    // отличие от bounding, который в MANUAL-модели всегда полноэкранный,
    // раз мы теперь рисуем на overlay КАЖДЫЙ кадр весь экран целиком).
    {
        xcb_xfixes_region_t emptyRegion = xcb_generate_id(conn);
        xcb_xfixes_create_region(conn, emptyRegion, 0, nullptr);
        xcb_xfixes_set_window_shape_region(conn, g_overlayWindow, XCB_SHAPE_SK_INPUT, 0, 0, emptyRegion);
        xcb_xfixes_destroy_region(conn, emptyRegion);
        xcb_flush(conn);
    }

    g_available = true;
    std::println("[ INFO ] VXR initialized (overlay window {}, MANUAL redirect mode)", g_overlayWindow);
    return true;
}

bool isAvailable() {
    return g_available;
}

void registerWindow(xcb_connection_t* conn, PHLWINDOW w) {
    if (!g_available || !conn || !w)
        return;

    const uint64_t id = w->stableID();
    if (g_windows.contains(id))
        return;

    SWindowRenderState state;

    // MANUAL: сервер БОЛЬШЕ НЕ рисует это окно на экран сам — с этого
    // момента repaint() обязан рисовать его в каждом кадре, иначе окно
    // физически не будет видно (это отличие от v1/AUTOMATIC — там
    // непрозрачные окна сервер продолжал рисовать сам без нашего
    // участия; здесь такой "бесплатной" отрисовки больше нет).
    xcb_composite_redirect_window(conn, w->getX11Window(), XCB_COMPOSITE_REDIRECT_MANUAL);

    state.redirected = true;
    g_windows.emplace(id, state);
}

void unregisterWindow(xcb_connection_t* conn, PHLWINDOW w) {
    if (!conn || !w)
        return;

    const uint64_t id = w->stableID();
    auto it = g_windows.find(id);
    if (it == g_windows.end())
        return;

    if (it->second.picture != XCB_NONE)
        xcb_render_free_picture(conn, it->second.picture);
    if (it->second.alphaMask != XCB_NONE)
        xcb_render_free_picture(conn, it->second.alphaMask);
    if (it->second.pixmap != XCB_PIXMAP_NONE)
        xcb_free_pixmap(conn, it->second.pixmap);

    if (it->second.redirected)
        xcb_composite_unredirect_window(conn, w->getX11Window(), XCB_COMPOSITE_REDIRECT_MANUAL);

    g_windows.erase(it);
}

void setWindowOpacity(xcb_connection_t* conn, PHLWINDOW w, float opacity) {
    if (!g_available || !conn || !w)
        return;

    const uint64_t id = w->stableID();
    auto it = g_windows.find(id);
    if (it == g_windows.end())
        return;

    opacity = std::clamp(opacity, 0.0f, 1.0f);
    it->second.opacity = opacity;

    if (it->second.alphaMask != XCB_NONE) {
        xcb_render_free_picture(conn, it->second.alphaMask);
        it->second.alphaMask = XCB_NONE;
    }

    const uint16_t alpha16 = static_cast<uint16_t>(opacity * 0xFFFF);

    it->second.alphaMask = xcb_generate_id(conn);
    xcb_render_create_solid_fill(conn, it->second.alphaMask,
                                  xcb_render_color_t{alpha16, alpha16, alpha16, alpha16});
}

void repaint(xcb_connection_t* conn, xcb_window_t root) {
    if (!g_available || !conn)
        return;

    // Троттлинг — тот же паттерн, что AnimationManager::tick(). Без
    // него repaint() (полный composite всего экрана!) дёргался бы на
    // каждой итерации busy-loop — то, что и вызвало зависание в v1.
    using clock_t = std::chrono::steady_clock;
    static clock_t::time_point lastRepaint{};
    static bool                haveLastRepaint = false;
    constexpr double           TARGET_FPS      = 60.0;
    constexpr double           MIN_INTERVAL    = 1.0 / TARGET_FPS;

    const auto now = clock_t::now();
    if (haveLastRepaint) {
        const double sinceLastRepaint = std::chrono::duration<double>(now - lastRepaint).count();
        if (sinceLastRepaint < MIN_INTERVAL)
            return;
    }
    lastRepaint     = now;
    haveLastRepaint = true;

    // Раньше тут был ранний return при g_windows.empty() — но splash
    // должен быть виден ДАЖЕ без единого открытого окна (сразу после
    // старта Vexland, до первого xterm/приложения), так что убираем
    // этот early-exit; если и окон, и активного splash нет — ниже всё
    // равно отрисуется просто фон, что дёшево.
    if (g_windows.empty() && !Splash::isActive())
        return;

    // Актуальный stacking order через query_tree — дёшево относительно
    // самого composite pass, и гарантированно корректно (не нужно
    // самим отслеживать XCB_CONFIGURE_NOTIFY/CIRCULATE_NOTIFY, чего у
    // нас пока нигде не реализовано).
    std::vector<xcb_window_t> stackOrder;
    {
        auto treeCookie = xcb_query_tree(conn, root);
        auto treeReply  = xcb_query_tree_reply(conn, treeCookie, nullptr);
        if (!treeReply)
            return;

        auto* children   = xcb_query_tree_children(treeReply);
        const int count  = xcb_query_tree_children_length(treeReply);

        for (int i = 0; i < count; ++i)
            stackOrder.push_back(children[i]);

        free(treeReply);
    }

    const auto rootGeomCookie = xcb_get_geometry(conn, root);
    auto       rootGeomReply  = xcb_get_geometry_reply(conn, rootGeomCookie, nullptr);
    if (!rootGeomReply)
        return;
    const uint16_t rootW = rootGeomReply->width;
    const uint16_t rootH = rootGeomReply->height;
    free(rootGeomReply);

    // КРИТИЧНО: double buffering. Раньше кадр строился ПОЭТАПНО прямо
    // на видимом overlay (залить чёрным -> нарисовать окно 1 ->
    // нарисовать окно 2 -> ...), и между этими отдельными X11-запросами
    // сервер иногда успевал показать промежуточное состояние на
    // реальном дисплее — отсюда мигание, замеченное на практике.
    // Теперь весь кадр собирается в offscreen pixmap, и на overlay
    // копируется УЖЕ ГОТОВЫЙ результат одним composite в конце.
    xcb_pixmap_t backBuffer = xcb_generate_id(conn);
    xcb_create_pixmap(conn, /*depth=*/32, backBuffer, root, rootW, rootH);

    xcb_render_picture_t backBufferPicture = xcb_generate_id(conn);
    xcb_render_create_picture(conn, backBufferPicture, backBuffer, g_formatARGB32, 0, nullptr);

    // Фон: пытаемся взять реальные обои через _XROOTPMAP_ID (см.
    // findRootBackgroundPixmap выше). Если не найдены (wallpaper-сеттер
    // не запущен, или использует другой механизм) — сплошная чёрная
    // заливка как безопасный fallback, лучше однотонный цвет, чем
    // мусорные/неопределённые пиксели.
    {
        const xcb_pixmap_t wallpaperPixmap = findRootBackgroundPixmap(conn, root);

        bool wallpaperDrawn = false;

        if (wallpaperPixmap != XCB_PIXMAP_NONE) {
            // НЕ предполагаем 24-бит — та же ошибка, что мы уже чинили
            // для окон и overlay (жёсткий формат вместо реальной
            // глубины даёт BadMatch/тихий провал create_picture).
            // wallpaper pixmap может быть создан с любой глубиной в
            // зависимости от того, как его создал сеттер (feh и т.п.).
            auto geomCookie = xcb_get_geometry(conn, wallpaperPixmap);
            auto geomReply  = xcb_get_geometry_reply(conn, geomCookie, nullptr);

            if (geomReply) {
                const xcb_render_pictformat_t wallpaperFormat = formatForWindowDepth(geomReply->depth);
                free(geomReply);

                if (wallpaperFormat) {
                    xcb_render_picture_t wallpaperPicture = xcb_generate_id(conn);
                    xcb_render_create_picture(conn, wallpaperPicture, wallpaperPixmap, wallpaperFormat, 0, nullptr);

                    xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, wallpaperPicture, XCB_NONE, backBufferPicture,
                                          0, 0, 0, 0, 0, 0, rootW, rootH);

                    xcb_render_free_picture(conn, wallpaperPicture);
                    wallpaperDrawn = true;
                }
            }
        }

        if (!wallpaperDrawn) {
            xcb_render_picture_t bgFill = xcb_generate_id(conn);
            xcb_render_create_solid_fill(conn, bgFill, xcb_render_color_t{0, 0, 0, 0xFFFF});
            xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, bgFill, XCB_NONE, backBufferPicture,
                                  0, 0, 0, 0, 0, 0, rootW, rootH);
            xcb_render_free_picture(conn, bgFill);
        }
    }

    // Splash-текст — рисуем ПОСЛЕ фона, но ДО окон (по z-order это
    // часть "обоев", терминалы должны перекрывать его, а не наоборот).
    // Позиция: снизу по центру экрана, в скруглённом полупрозрачном
    // "пузыре" (как в мессенджерах) вместо голого текста на обоях.
    if (Splash::isActive()) {
        int textWidth = 0, textHeight = 0;
        constexpr int SPLASH_SCALE   = 3;  // 15x21px на символ — читаемо на большинстве экранов
        constexpr int BOTTOM_MARGIN  = 40;
        constexpr int BUBBLE_PADDING = 14; // отступ текста от края пузыря со всех сторон
        constexpr int BUBBLE_RADIUS  = 12;

        xcb_pixmap_t textMask = BitmapFont::rasterizeText(conn, backBuffer, Splash::text(), SPLASH_SCALE, &textWidth, &textHeight);

        if (textMask != XCB_PIXMAP_NONE && textWidth > 0) {
            xcb_render_pictformat_t maskFormat = 0;
            {
                // depth=1 pict format — не то же самое, что ARGB32/RGB24
                // (formatForWindowDepth их не покрывает, там только 24/32).
                // Запрашиваем отдельно здесь: для bitmap-маски нужен формат
                // с type=DIRECT и depth=1 (обычно "a1" алиас в XRender).
                auto cookie = xcb_render_query_pict_formats(conn);
                auto reply  = xcb_render_query_pict_formats_reply(conn, cookie, nullptr);
                if (reply) {
                    auto* formats  = xcb_render_query_pict_formats_formats(reply);
                    const int cnt  = xcb_render_query_pict_formats_formats_length(reply);
                    for (int i = 0; i < cnt; ++i) {
                        if (formats[i].depth == 1) {
                            maskFormat = formats[i].id;
                            break;
                        }
                    }
                    free(reply);
                }
            }

            if (maskFormat) {
                const float    alpha    = Splash::currentAlpha();
                const uint16_t alpha16  = static_cast<uint16_t>(std::clamp(alpha, 0.0f, 1.0f) * 0xFFFF);

                const int bubbleWidth  = textWidth + 2 * BUBBLE_PADDING;
                const int bubbleHeight = textHeight + 2 * BUBBLE_PADDING;
                const int16_t bubbleX  = static_cast<int16_t>((rootW - bubbleWidth) / 2);
                const int16_t bubbleY  = static_cast<int16_t>(rootH - BOTTOM_MARGIN - bubbleHeight);

                // --- Пузырь: тёмная полупрозрачная скруглённая подложка ---
                // Alpha пузыря чуть ниже, чем у текста (0.6 от текущей
                // splash-альфы) — так подложка визуально "легче" текста,
                // ближе к ощущению настоящего message bubble, а не
                // сплошного прямоугольника.
                {
                    const auto rects = buildRoundedBubbleRects(bubbleWidth, bubbleHeight, BUBBLE_RADIUS);

                    const uint16_t bubbleAlpha16 = static_cast<uint16_t>(alpha16 * 0.6);
                    xcb_render_picture_t bubbleFill = xcb_generate_id(conn);
                    // Premultiplied alpha: цвет тоже умножаем на alpha-долю,
                    // иначе тёмный полупрозрачный пузырь на светлом фоне
                    // будет выглядеть светлее, чем задумано (XRender
                    // ожидает premultiplied для OP_OVER).
                    const uint16_t bubbleColorComponent = static_cast<uint16_t>(0x1a1a * (bubbleAlpha16 / static_cast<double>(0xFFFF)));
                    xcb_render_create_solid_fill(conn, bubbleFill,
                                                  xcb_render_color_t{bubbleColorComponent, bubbleColorComponent, bubbleColorComponent, bubbleAlpha16});

                    // Рисуем каждый прямоугольник пузыря отдельным
                    // composite — bounding shape тут не нужен (это не
                    // окно), просто рисуем ровно те области, что формируют
                    // скруглённую форму.
                    for (const auto& rect : rects) {
                        xcb_render_composite(conn, XCB_RENDER_PICT_OP_OVER, bubbleFill, XCB_NONE, backBufferPicture,
                                              0, 0, 0, 0,
                                              static_cast<int16_t>(bubbleX + rect.x), static_cast<int16_t>(bubbleY + rect.y),
                                              rect.width, rect.height);
                    }

                    xcb_render_free_picture(conn, bubbleFill);
                }

                // --- Текст поверх пузыря ---
                xcb_render_picture_t maskPicture = xcb_generate_id(conn);
                xcb_render_create_picture(conn, maskPicture, textMask, maskFormat, 0, nullptr);

                xcb_render_picture_t colorFill = xcb_generate_id(conn);
                xcb_render_create_solid_fill(conn, colorFill, xcb_render_color_t{alpha16, alpha16, alpha16, alpha16});

                const int16_t textX = static_cast<int16_t>(bubbleX + BUBBLE_PADDING);
                const int16_t textY = static_cast<int16_t>(bubbleY + BUBBLE_PADDING);

                xcb_render_composite(conn, XCB_RENDER_PICT_OP_OVER, colorFill, maskPicture, backBufferPicture,
                                      0, 0, 0, 0, textX, textY, static_cast<uint16_t>(textWidth), static_cast<uint16_t>(textHeight));

                xcb_render_free_picture(conn, colorFill);
                xcb_render_free_picture(conn, maskPicture);
            }
        }

        if (textMask != XCB_PIXMAP_NONE)
            xcb_free_pixmap(conn, textMask);
    }

    // Рисуем окна СНИЗУ ВВЕРХ по stacking order — query_tree отдаёт
    // детей в bottom-to-top порядке (соответствует протоколу X11 core:
    // "listed in bottom-to-top stacking order"), так что просто идём
    // по списку как есть. Всё ещё в backBuffer, НЕ на overlay напрямую.
    for (const auto xwin : stackOrder) {
        auto w = CWindowState::get()->byXWindow(xwin);
        if (!w)
            continue; // это не наше окно (root decorations, override-redirect и т.п.) — пропускаем

        auto it = g_windows.find(w->stableID());
        if (it == g_windows.end())
            continue; // не зарегистрировано в VXR

        if (!refreshWindowPicture(conn, w, it->second))
            continue; // окно исчезло между query_tree и сейчас, или необычная глубина

        const auto x      = static_cast<int16_t>(w->x());
        const auto y      = static_cast<int16_t>(w->y());
        const auto width  = static_cast<uint16_t>(w->w());
        const auto height = static_cast<uint16_t>(w->h());

        xcb_render_composite(conn, XCB_RENDER_PICT_OP_OVER, it->second.picture, it->second.alphaMask, backBufferPicture,
                              0, 0, 0, 0, x, y, width, height);
    }

    // Единственная операция, которая реально видна пользователю —
    // один атомарный SRC composite готового кадра на overlay.
    xcb_render_picture_t overlayPicture = xcb_generate_id(conn);
    xcb_render_create_picture(conn, overlayPicture, g_overlayWindow, g_overlayFormat, 0, nullptr);

    xcb_render_composite(conn, XCB_RENDER_PICT_OP_SRC, backBufferPicture, XCB_NONE, overlayPicture,
                          0, 0, 0, 0, 0, 0, rootW, rootH);

    xcb_render_free_picture(conn, overlayPicture);
    xcb_render_free_picture(conn, backBufferPicture);
    xcb_free_pixmap(conn, backBuffer);
    xcb_flush(conn);
}

} // namespace VXR
