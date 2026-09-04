#include "WindowShape.hpp"
#include "../config/ConfigManager.hpp"

extern "C" {
#include <xcb/shape.h>
}

#include <vector>
#include <cmath>
#include <algorithm>
#include <print>

namespace WindowShape {

namespace {

// Число "ступенек" на угол. Больше = более гладкий вид, но больше
// прямоугольников в запросе (каждый — отдельный xcb_rectangle_t).
// 4 — разумный компромисс для radius 6-12px, к которым мы и стремимся
// (см. дефолт general.rounding в ConfigManager ниже).
constexpr int STEPS_PER_CORNER = 4;

// Строит список xcb_rectangle_t, покрывающих окно ЗА ВЫЧЕТОМ 4 угловых
// "лестниц" — то есть это и есть финальная видимая форма окна.
// Подход: генерируем region как объединение горизонтальных полос,
// каждая полоса уже сама учитывает, насколько её нужно "подрезать"
// слева/справа около верхнего/нижнего края (там где радиус ещё активен).
std::vector<xcb_rectangle_t> buildRoundedRegion(int width, int height, int radius) {
    std::vector<xcb_rectangle_t> rects;

    if (radius <= 0 || width <= 0 || height <= 0) {
        rects.push_back(xcb_rectangle_t{0, 0, static_cast<uint16_t>(std::max(width, 0)), static_cast<uint16_t>(std::max(height, 0))});
        return rects;
    }

    // Не даём радиусу быть больше половины меньшей стороны — иначе
    // "лестницы" верхнего и нижнего угла пересекутся и посчитают
    // отрицательную высоту полосы.
    radius = std::min(radius, std::min(width, height) / 2);

    // Средняя часть окна (между зоной скругления сверху и снизу) —
    // один прямоугольник на всю ширину, т.к. там скругление не
    // действует вообще.
    if (height > 2 * radius) {
        rects.push_back(xcb_rectangle_t{0, static_cast<int16_t>(radius), static_cast<uint16_t>(width), static_cast<uint16_t>(height - 2 * radius)});
    }

    // Верхняя и нижняя "лестницы" — по STEPS_PER_CORNER горизонтальных
    // полосок каждая, ширина полоски сужается по мере приближения к
    // самому краю (аппроксимация четверть-круга через уравнение
    // окружности x^2 + y^2 = r^2, решённое относительно x на каждой
    // дискретной высоте y).
    for (int step = 0; step < STEPS_PER_CORNER; ++step) {
        // y-диапазон этой ступеньки внутри зоны радиуса [0, radius)
        const double yTopOfStep    = (static_cast<double>(step) / STEPS_PER_CORNER) * radius;
        const double yBottomOfStep = (static_cast<double>(step + 1) / STEPS_PER_CORNER) * radius;

        // На верхней границе полоски (ближе к краю окна = меньше x
        // выреза; берём y = yTopOfStep, т.к. чем ближе к самому краю,
        // тем сильнее срез). Используем max по обеим границам, чтобы
        // полоска не вылезала за реальную окружность на своём стыке
        // со соседней ступенькой (иначе на глаз будет видна ступенька
        // "наружу" эллипса, а не внутрь).
        auto xInsetAtY = [radius](double y) -> int {
            const double dy = radius - y; // расстояние от нижнего (внутреннего) края зоны скругления
            const double underSqrt = static_cast<double>(radius) * radius - dy * dy;
            if (underSqrt <= 0.0)
                return radius;
            return radius - static_cast<int>(std::floor(std::sqrt(underSqrt)));
        };

        const int insetTop    = xInsetAtY(yTopOfStep);
        const int insetBottom = xInsetAtY(yBottomOfStep);
        const int inset       = std::max(insetTop, insetBottom); // консервативно — не даём "выпирать" за окружность

        const int16_t  stripY      = static_cast<int16_t>(std::floor(yTopOfStep));
        const int16_t  stripYBot   = static_cast<int16_t>(std::ceil(yBottomOfStep));
        const uint16_t stripHeight = static_cast<uint16_t>(std::max(1, stripYBot - stripY));

        if (width - 2 * inset > 0) {
            // верхняя полоска
            rects.push_back(xcb_rectangle_t{static_cast<int16_t>(inset), stripY, static_cast<uint16_t>(width - 2 * inset), stripHeight});
            // симметричная нижняя полоска (зеркально от низа окна)
            const int16_t bottomStripY = static_cast<int16_t>(height - stripY - stripHeight);
            rects.push_back(xcb_rectangle_t{static_cast<int16_t>(inset), bottomStripY, static_cast<uint16_t>(width - 2 * inset), stripHeight});
        }
    }

    return rects;
}

bool g_shapeExtAvailable = false;

} // namespace

bool queryShapeExtension(xcb_connection_t* conn) {
    if (!conn) {
        g_shapeExtAvailable = false;
        return false;
    }

    const xcb_query_extension_reply_t* ext = xcb_get_extension_data(conn, &xcb_shape_id);
    g_shapeExtAvailable                    = ext && ext->present;

    std::println("[ SHAPEDEBUG ] queryShapeExtension: ext={} present={} -> available={}",
                 ext != nullptr, ext ? ext->present : false, g_shapeExtAvailable);

    if (!g_shapeExtAvailable)
        std::println(stderr, "[ WARN ] X11 Shape extension not present on this server — rounded corners disabled");

    return g_shapeExtAvailable;
}

void applyRounding(xcb_connection_t* conn, PHLWINDOW w, int radius) {
    // Не проверяем w->m_isMapped здесь — в отличие от applyBorder(),
    // эта функция вызывается из applyCurrentGeometryToX11(), которая на
    // ПЕРВОМ позиционировании окна (warp=true из dwindle-алгоритма при
    // создании target) срабатывает РАНЬШЕ, чем main.cpp успевает
    // выставить w->m_isMapped = true (это происходит уже после
    // CLayoutManager::newTarget()). Shape extension работает по
    // валидному xcb_window_t на сервере — ему не важен наш внутренний
    // флаг "замаплено ли логически", так что эта проверка была бы
    // излишним (и, как выяснилось на практике, ошибочным) заимствованием
    // из applyBorder().
    if (!conn || !w || !g_shapeExtAvailable) {
        std::println("[ SHAPEDEBUG ] applyRounding SKIPPED: conn={} w={} extAvailable={}",
                     conn != nullptr, w != nullptr, g_shapeExtAvailable);
        return;
    }

    const int clientWidth  = static_cast<int>(w->w());
    const int clientHeight = static_cast<int>(w->h());

    if (clientWidth <= 0 || clientHeight <= 0) {
        std::println("[ SHAPEDEBUG ] applyRounding SKIPPED: bad size width={} height={}", clientWidth, clientHeight);
        return;
    }

    // КРИТИЧНО: XCB_SHAPE_SK_BOUNDING работает в системе координат
    // ВСЕГО окна, включая border — (0,0) это угол border, не client
    // area. w->w()/w->h() — это именно client area (то же самое, что мы
    // передаём в XCB_CONFIG_WINDOW_WIDTH/HEIGHT, border в это НЕ
    // входит, X11 рисует его поверх, снаружи). Если строить маску
    // размером ровно client area без поправки — bounding shape обрежет
    // border по бокам/снизу до нуля, останется только та часть, что
    // случайно совпала с верхним краем. Отсюда и баг "рамка видна
    // только сверху".
    const auto* cfg          = CConfigManager::get();
    const int   borderWidth  = cfg ? static_cast<int>(cfg->getFloat("general.border_size", 2.0)) : 2;

    const int fullWidth  = clientWidth + 2 * borderWidth;
    const int fullHeight = clientHeight + 2 * borderWidth;

    auto rects = buildRoundedRegion(fullWidth, fullHeight, radius);

    std::println("[ SHAPEDEBUG ] applyRounding: xwin={} radius={} client={}x{} border={} full={}x{} rectCount={}",
                 w->getX11Window(), radius, clientWidth, clientHeight, borderWidth, fullWidth, fullHeight, rects.size());

    // XCB_SHAPE_SO_SET — заменяет всю bounding-маску окна целиком
    // (не аддитивно к предыдущей форме — важно, иначе повторные
    // resize накапливали бы старые вырезы).
    auto cookie = xcb_shape_rectangles_checked(conn, XCB_SHAPE_SO_SET, XCB_SHAPE_SK_BOUNDING, XCB_CLIP_ORDERING_UNSORTED, w->getX11Window(),
                                                0, 0, static_cast<uint32_t>(rects.size()), rects.data());
    auto err = xcb_request_check(conn, cookie);
    if (err) {
        std::println("[ SHAPEDEBUG ] xcb_shape_rectangles FAILED: error_code={}", err->error_code);
        free(err);
    }

    xcb_flush(conn);
}

} // namespace WindowShape
