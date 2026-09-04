#include "AnimationManager.hpp"
#include "../desktop/view/WindowState.hpp"
#include "../config/ConfigManager.hpp"

#include <chrono>
#include <cmath>
#include <algorithm>

namespace AnimationManager {

namespace {

using clock_t = std::chrono::steady_clock;

// Момент последнего РЕАЛЬНО применённого кадра (не момент вызова tick()
// — тот дёргается почти на каждой итерации event loop busy-loop'а, но
// мы throttle'им независимо от частоты вызовов).
clock_t::time_point g_lastFrameTime{};
bool                g_haveLastFrame = false;

double nowSeconds() {
    return std::chrono::duration<double>(clock_t::now().time_since_epoch()).count();
}

// Длительность анимации move/resize в секундах, из конфига
// (general.anim_duration_ms), дефолт — быстро, но заметно.
double animDurationSeconds() {
    auto* cfg = CConfigManager::get();
    const double ms = cfg ? cfg->getFloat("general.anim_duration_ms", 160.0) : 160.0;
    return std::max(1.0, ms) / 1000.0;
}

// Троттлинг — не применяем geometry чаще целевого FPS, даже если tick()
// зовут значительно чаще (event loop крутится ~1000 раз/сек благодаря
// usleep(1000), нам не нужно xcb_configure_window на каждой итерации).
constexpr double TARGET_FPS         = 60.0;
constexpr double MIN_FRAME_INTERVAL = 1.0 / TARGET_FPS;

// ease-out cubic: быстрый старт, плавное замедление к цели — ощущается
// отзывчивее linear/ease-in-out для UI move/resize анимаций.
double easeOutCubic(double t) {
    const double inv = 1.0 - t;
    return 1.0 - inv * inv * inv;
}

double lerp(double begin, double goal, double t) {
    return begin + (goal - begin) * t;
}

// Один шаг анимации для конкретного окна, elapsed считается от
// собственного anim.animStartSeconds этого окна (окна анимируют
// независимо, могли стартовать в разные моменты).
void stepWindow(const PHLWINDOW& w, double now, double duration) {
    auto& anim = w->animState();

    if (!anim.animating)
        return;

    const double elapsed = now - anim.animStartSeconds;
    const double t        = std::clamp(elapsed / duration, 0.0, 1.0);
    const double e        = easeOutCubic(t);

    anim.curX = lerp(anim.beginX, anim.goalX, e);
    anim.curY = lerp(anim.beginY, anim.goalY, e);
    anim.curW = lerp(anim.beginW, anim.goalW, e);
    anim.curH = lerp(anim.beginH, anim.goalH, e);

    if (t >= 1.0) {
        anim.curX     = anim.goalX;
        anim.curY     = anim.goalY;
        anim.curW     = anim.goalW;
        anim.curH     = anim.goalH;
        anim.animating = false;
    }

    w->applyCurrentGeometryToX11();
}

} // namespace

void tick() {
    const auto now = clock_t::now();

    if (!g_haveLastFrame) {
        g_lastFrameTime = now;
        g_haveLastFrame = true;
        return;
    }

    const double sinceLastFrame = std::chrono::duration<double>(now - g_lastFrameTime).count();
    if (sinceLastFrame < MIN_FRAME_INTERVAL)
        return;

    g_lastFrameTime = now;

    const double nowS     = nowSeconds();
    const double duration = animDurationSeconds();

    for (const auto& w : CWindowState::get()->all()) {
        if (w)
            stepWindow(w, nowS, duration);
    }
}

} // namespace AnimationManager
