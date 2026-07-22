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
constexpr unsigned int kColorSimFlow = 0xFFC8C83C;      // teal: X-Plane's own ATC flow rules, stronger than a wind guess
constexpr unsigned int kColorWaiting = 0xFF8C8C8C;      // dim gray: history / no data yet
// Sky blue -- distinct from the status colors above (wind confidence is a
// different axis from runway status). Also doubles as the general chrome
// accent (tabs/headers/buttons) below.
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
// constants above. Must be called after an ImGui context exists (i.e. from
// MainWindow's constructor, not MainWindow::InitFontAtlas, which runs
// earlier) since it touches ImGui::GetStyle().
void ApplyTheme();

// Font Awesome 6 Free Solid glyphs (third_party/FontAwesome, SIL OFL 1.1),
// merged into the shared font atlas alongside DejaVuSans -- see
// MainWindow::InitFontAtlas. Splice directly into a format string, e.g.
// ImGui::Text("%s Departures", kIconDeparture). Every codepoint used here
// must also be listed in kIconGlyphCodepoints below, or it renders as a
// blank/tofu glyph.
constexpr const char* kIconConfirmed = "\xEF\x81\x98";    // f058 circle-check
constexpr const char* kIconWindEstimate = "\xEF\x81\x99"; // f059 circle-question
constexpr const char* kIconWaiting = "\xEF\x80\x97";      // f017 clock
constexpr const char* kIconWind = "\xEF\x9C\xAE";         // f72e wind
constexpr const char* kIconAltimeter = "\xEF\x98\xA4";    // f624 gauge
constexpr const char* kIconDeparture = "\xEF\x96\xB0";    // f5b0 plane-departure
constexpr const char* kIconArrival = "\xEF\x96\xAF";      // f5af plane-arrival
constexpr const char* kIconNearby = "\xEF\x8F\x85";       // f3c5 location-dot
constexpr const char* kIconDashboardTab = "\xEF\x81\xB2";  // f072 plane
constexpr const char* kIconFlightPlanTab = "\xEF\x93\x97"; // f4d7 route
constexpr const char* kIconHistoryTab = "\xEF\x87\x9A";    // f1da clock-rotate-left
constexpr const char* kIconSettingsTab = "\xEF\x80\x93";   // f013 gear
constexpr const char* kIconDebug = "\xEF\x86\x88";         // f188 bug
// Settings-tab section headers.
constexpr const char* kIconDisplaySection = "\xEF\x8E\x90"; // f390 desktop
constexpr const char* kIconSearchSection = "\xEF\x80\x82";  // f002 magnifying-glass
constexpr const char* kIconStartupSection = "\xEF\x80\x91"; // f011 power-off
constexpr const char* kIconArrowRight = "\xEF\x81\xA1";      // f061 arrow-right
constexpr const char* kIconSimFlow = "\xEF\x81\x9A";        // f05a circle-info

constexpr std::array<unsigned short, 18> kIconGlyphCodepoints = {
    0xF058, 0xF059, 0xF017, 0xF72E, 0xF624, 0xF5B0, 0xF5AF, 0xF3C5, 0xF072, 0xF1DA,
    0xF013, 0xF188, 0xF390, 0xF002, 0xF011, 0xF4D7, 0xF061, 0xF05A,
};

constexpr int kDefaultWindowWidth = 560;
constexpr int kDefaultWindowHeight = 700;
constexpr int kMinWindowWidth = 320;
constexpr int kMinWindowHeight = 260;
constexpr int kMaxWindowWidth = 900;
constexpr int kMaxWindowHeight = 900;

// Text auto-scales with window width (MainWindow::ComputeAutoTextScale) --
// these are just a floor/ceiling safety net.
constexpr float kMinAutoTextScale = 0.6f;
constexpr float kMaxAutoTextScale = 2.0f;

// RenderRunwayDiagram's base diameter at 1x scale. MainWindow scales this by
// the same auto-text-scale factor as the rest of the UI (not independently)
// so runway-ID label size and runway spacing stay proportional at every
// window size -- otherwise labels (which do scale with text) crowd a
// diagram whose geometry doesn't.
constexpr float kRunwayDiagramDiameter = 150.0f;
// Applied on top of kRunwayDiagramDiameter (see RenderCenteredRunwayDiagram,
// MainWindow.cpp) so the diagram reads as a centerpiece rather than small
// against a full-width card.
constexpr float kRunwayDiagramSizeMultiplier = 2.0f;

// Extra strip to the right of the compass circle reserved for the windsock
// icon. Shared with MainWindow.cpp (not just Widgets.cpp) for centering the
// diagram within its available card width.
constexpr float kWindsockStripWidth = 50.0f;

// Preset lists offered in the Settings tab.
constexpr std::array<int, 8> kSearchRadiusPresetsNm = {5, 10, 15, 20, 30, 50, 75, 100};
constexpr std::array<int, 10> kMaxAirportsPresets = {1, 2, 3, 5, 8, 10, 15, 20, 30, 50};
constexpr std::array<int, 5> kActiveWindowPresetsMin = {5, 15, 30, 45, 60};

} // namespace trm::ui
