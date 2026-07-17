#pragma once

#include <array>

// Shared visual constants for ui/MainWindow + ui/Widgets: colors, preset
// lists, and window sizing. ApplyTheme() (Theme.cpp) is the one place that
// pushes these into ImGuiStyle; everything else just reads the constants.

namespace trm::ui {

// Builds a packed ABGR color (hex literal 0xAABBGGRR) from separate
// channels, so new palette entries below don't need hand-computed hex.
constexpr unsigned int Rgba(unsigned char r, unsigned char g, unsigned char b, unsigned char a = 255)
{
    return (static_cast<unsigned int>(a) << 24) | (static_cast<unsigned int>(b) << 16) |
           (static_cast<unsigned int>(g) << 8) | static_cast<unsigned int>(r);
}

// Packed ABGR (ImGui's native color format).
constexpr unsigned int kColorConfirmed = 0xFF3CC83C;    // green: real, traffic-confirmed runway
constexpr unsigned int kColorWindEstimate = 0xFF28AAE6; // amber: wind-based guess only
constexpr unsigned int kColorWaiting = 0xFF8C8C8C;      // dim gray: history / no data yet
// Sky blue, deliberately distinct from the runway-status colors above --
// wind confidence (own station / regional / etc.) is a different axis from
// runway status (confirmed / estimated / waiting) and amber already means
// the latter, so reusing it here would blur two unrelated meanings. Also
// doubles as the general chrome accent (tabs/headers/buttons) below -- it's
// the one hue in the palette not already claimed by a runway-status meaning.
constexpr unsigned int kColorWind = 0xFFE6AA3C;
constexpr unsigned int kColorAccent = kColorWind;
// Real ICAO windsock banding -- international orange alternating with white,
// deliberately realistic colors rather than app-palette colors, so the icon
// reads as "a windsock" at a glance.
constexpr unsigned int kColorWindsockOrange = 0xFF008CFF;
constexpr unsigned int kColorWindsockWhite = 0xFFE6E6E6;

// Neutral chrome palette -- dark, cool-toned, stepped from window background
// (darkest) up through panel/card, frame, and its hover/active states.
constexpr unsigned int kColorBgWindow = Rgba(18, 20, 24);
constexpr unsigned int kColorBgPanel = Rgba(26, 29, 35);
constexpr unsigned int kColorBgFrame = Rgba(34, 38, 46);
constexpr unsigned int kColorBgFrameHovered = Rgba(42, 47, 58);
constexpr unsigned int kColorBgFrameActive = Rgba(50, 56, 68);
constexpr unsigned int kColorBorder = Rgba(52, 58, 70, 140);
constexpr unsigned int kColorTextPrimary = Rgba(230, 232, 236);

// Rounding (px) -- applied window-wide by ApplyTheme().
constexpr float kUiWindowRounding = 6.0f;
constexpr float kUiFrameRounding = 4.0f;
constexpr float kUiPopupRounding = 6.0f;
constexpr float kUiScrollbarRounding = 6.0f;
constexpr float kUiGrabRounding = 4.0f;
constexpr float kUiTabRounding = 6.0f;
// Card corner rounding used by widgets that draw their own background rect
// (e.g. RenderAirportCard) rather than going through ImGuiStyle.
constexpr float kUiCardRounding = 6.0f;

// Spacing/padding (px) -- applied window-wide by ApplyTheme().
constexpr float kUiWindowPaddingX = 12.0f;
constexpr float kUiWindowPaddingY = 10.0f;
constexpr float kUiFramePaddingX = 8.0f;
constexpr float kUiFramePaddingY = 4.0f;
constexpr float kUiItemSpacingX = 8.0f;
constexpr float kUiItemSpacingY = 6.0f;
constexpr float kUiItemInnerSpacingX = 6.0f;
constexpr float kUiItemInnerSpacingY = 4.0f;

// Sets ImGuiStyle (rounding, spacing, full ImGuiCol_* palette) from the
// constants above. Call once at startup, before any MainWindow frame is
// built -- see MainWindow::InitFontAtlas, the existing one-time-init entry
// point. Touches ImGui::GetStyle() directly; no per-window state.
void ApplyTheme();

// Font Awesome 6 Free Solid glyphs (third_party/FontAwesome, SIL OFL 1.1),
// merged into the shared font atlas alongside DejaVuSans -- see
// MainWindow::InitFontAtlas. Each constant is the icon's UTF-8 encoding, so
// callers just splice it into a format string, e.g.
// ImGui::Text("%s Departures", kIconDeparture). kIconGlyphCodepoints below
// must list every codepoint used by the constants in this block -- it's
// what MainWindow::InitFontAtlas hands to ImGui as the merge font's glyph
// range, so a codepoint missing from it renders as a blank/tofu glyph.
constexpr const char* kIconConfirmed = "\xEF\x81\x98";    // f058 circle-check
constexpr const char* kIconWindEstimate = "\xEF\x81\x99"; // f059 circle-question
constexpr const char* kIconWaiting = "\xEF\x80\x97";      // f017 clock
constexpr const char* kIconWind = "\xEF\x9C\xAE";         // f72e wind
constexpr const char* kIconAltimeter = "\xEF\x98\xA4";    // f624 gauge
constexpr const char* kIconDeparture = "\xEF\x96\xB0";    // f5b0 plane-departure
constexpr const char* kIconArrival = "\xEF\x96\xAF";      // f5af plane-arrival
constexpr const char* kIconNearby = "\xEF\x8F\x85";       // f3c5 location-dot
constexpr const char* kIconDashboardTab = "\xEF\x81\xB2"; // f072 plane
constexpr const char* kIconHistoryTab = "\xEF\x87\x9A";   // f1da clock-rotate-left
constexpr const char* kIconSettingsTab = "\xEF\x80\x93";  // f013 gear
constexpr const char* kIconDebug = "\xEF\x86\x88";        // f188 bug

constexpr std::array<unsigned short, 12> kIconGlyphCodepoints = {
    0xF058, 0xF059, 0xF017, 0xF72E, 0xF624, 0xF5B0, 0xF5AF, 0xF3C5, 0xF072, 0xF1DA, 0xF013, 0xF188,
};

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
