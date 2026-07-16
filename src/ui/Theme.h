#pragma once

#include <array>

// Shared visual constants for ui/MainWindow + ui/Widgets: colors, preset
// lists, and window sizing.

namespace trm::ui {

// Packed ABGR (ImGui's native color format).
constexpr unsigned int kColorConfirmed = 0xFF3CC83C;    // green: real, traffic-confirmed runway
constexpr unsigned int kColorWindEstimate = 0xFF28AAE6; // amber: wind-based guess only
constexpr unsigned int kColorWaiting = 0xFF8C8C8C;      // dim gray: history / no data yet
// Sky blue, deliberately distinct from the runway-status colors above --
// wind confidence (own station / regional / etc.) is a different axis from
// runway status (confirmed / estimated / waiting) and amber already means
// the latter, so reusing it here would blur two unrelated meanings.
constexpr unsigned int kColorWind = 0xFFE6AA3C;
// Real ICAO windsock banding -- international orange alternating with white,
// deliberately realistic colors rather than app-palette colors, so the icon
// reads as "a windsock" at a glance.
constexpr unsigned int kColorWindsockOrange = 0xFF008CFF;
constexpr unsigned int kColorWindsockWhite = 0xFFE6E6E6;

constexpr int kDefaultWindowWidth = 560;
constexpr int kDefaultWindowHeight = 700;
constexpr int kMinWindowWidth = 320;
constexpr int kMinWindowHeight = 260;
constexpr int kMaxWindowWidth = 900;
constexpr int kMaxWindowHeight = 900;

// Preset lists offered in the Settings tab.
constexpr std::array<int, 8> kSearchRadiusPresetsNm = {5, 10, 15, 20, 30, 50, 75, 100};
constexpr std::array<int, 10> kMaxAirportsPresets = {1, 2, 3, 5, 8, 10, 15, 20, 30, 50};
constexpr std::array<int, 5> kActiveWindowPresetsMin = {5, 15, 30, 45, 60};
constexpr std::array<float, 7> kTextSizePresets = {0.75f, 1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f};

} // namespace trm::ui
