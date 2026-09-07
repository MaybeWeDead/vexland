#include "Splash.hpp"
#include "../config/ConfigManager.hpp"

#include <vector>
#include <string>
#include <chrono>
#include <random>
#include <algorithm>

namespace Splash {

namespace {

// Обычные фразы — 99% шанс, равномерно между собой.
const std::vector<std::string>& normalPhrases() {
    static const std::vector<std::string> phrases = {
        "Made for the tablets Hyprland forgot",
        "No Wayland. No regrets.",
        "Your Nvidia card from 2011 called - it works here",
        "Lighter than your excuses for not tiling",
        "X11: still not dead, still not tired",
        "Compositing without the RAM tax",
        "Dwindle harder",
        "Rounded corners, not rounded promises",
        "VXR: because picom eats RAM for breakfast",
        "Somewhere, a Wayland dev is confused right now",
        "Runs on a tablet. Runs on your pride.",
        "Border size 2px, ego size unlimited",
        "We animate windows, not excuses",
        "Fullscreen: now actually full screen",
        "killactive, now with manners",
        "Built on a Galaxy Tab, out of spite",
        "No blur. No shadows. No mercy.",
        "Your rice, your rules, our tiling",
        "XCB core, zero cores wasted",
        "Legacy protocol, modern attitude",
        "gaps in this, gaps out that",
        "Written by a teenager, tested by nobody",
        "Compiled on a phone. Don't ask.",
        "Wayland, fuck you (with love, from X11)",
        "bigger. better. stronger. also lighter.",
        "Code hard. Rice hard.",
    };
    return phrases;
}

// Редкая пасхалка — 1% шанс.
constexpr const char* RARE_PHRASE = "Simon Says you're lucky";
constexpr double      RARE_CHANCE = 0.01;

constexpr double VISIBLE_DURATION_SECONDS = 30.0;
constexpr double FADE_DURATION_SECONDS    = 2.0;
constexpr double TOTAL_DURATION_SECONDS   = VISIBLE_DURATION_SECONDS + FADE_DURATION_SECONDS;

using clock_t = std::chrono::steady_clock;

clock_t::time_point g_startTime{};
bool                g_initialized = false;
std::string         g_chosenText;

} // namespace

void init() {
    std::mt19937                          rng(std::random_device{}());
    std::uniform_real_distribution<double> chanceDist(0.0, 1.0);

    if (chanceDist(rng) < RARE_CHANCE) {
        g_chosenText = RARE_PHRASE;
    } else {
        const auto& phrases = normalPhrases();
        std::uniform_int_distribution<size_t> indexDist(0, phrases.size() - 1);
        g_chosenText = phrases[indexDist(rng)];
    }

    g_startTime   = clock_t::now();
    g_initialized = true;
}

bool isActive() {
    if (!g_initialized)
        return false;

    auto* cfg = CConfigManager::get();
    if (cfg && cfg->getFloat("general.disable_splash", 0.0) != 0.0)
        return false;

    const double elapsed = std::chrono::duration<double>(clock_t::now() - g_startTime).count();
    return elapsed < TOTAL_DURATION_SECONDS;
}

float currentAlpha() {
    if (!g_initialized)
        return 0.0f;

    const double elapsed = std::chrono::duration<double>(clock_t::now() - g_startTime).count();

    if (elapsed < VISIBLE_DURATION_SECONDS)
        return 1.0f;

    if (elapsed >= TOTAL_DURATION_SECONDS)
        return 0.0f;

    const double fadeElapsed = elapsed - VISIBLE_DURATION_SECONDS;
    const double t           = fadeElapsed / FADE_DURATION_SECONDS;
    return static_cast<float>(std::clamp(1.0 - t, 0.0, 1.0));
}

const char* text() {
    return g_chosenText.c_str();
}

} // namespace Splash
