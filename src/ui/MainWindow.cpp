#include "ui/MainWindow.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <memory>

#include "XPLMUtilities.h"

#include "sdk/Log.h"
#include "sdk/PluginPath.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

// imconfig.h's IM_ASSERT (vendored as-is from LiveTraffic, see that file's
// own comment) calls this global-namespace function on every failed ImGui
// assertion. It's declared, never defined, anywhere in the vendored
// ImGui/ImgWindow snapshot -- every translation unit that hits an
// IM_ASSERT (i.e. all of imgui*.cpp) needs this symbol to link, so it's
// provided here rather than left dangling.
void LogFatalMsg(const char* szPath, int ln, const char* szFunc, const char* szMsg, ...)
{
    char formatted[768];
    va_list args;
    va_start(args, szMsg);
    std::vsnprintf(formatted, sizeof(formatted), szMsg, args);
    va_end(args);

    char full[1024];
    std::snprintf(full, sizeof(full), "ImGui assert failed (%s:%d, %s): %s", szPath, ln, szFunc, formatted);
    trm::sdk::Log(trm::sdk::LogLevel::Error, full);
}

namespace trm::ui {

namespace {

// Settings-tab section header. ImGui 1.78 WIP predates ImGui::SeparatorText,
// hence the manual icon+label+Separator() build here.
void RenderSectionHeader(const char* icon, const char* label)
{
    ImGui::PushStyleColor(ImGuiCol_Text, kColorAccent);
    ImGui::Text("%s %s", icon, label);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Spacing();
}

// Used both for ImGui::SetWindowFontScale (buildInterface) and to scale the
// runway diagram (RenderCenteredRunwayDiagram) by the same factor, so text
// and diagram geometry stay proportional at every window size.
float ComputeAutoTextScale()
{
    return std::clamp(ImGui::GetWindowWidth() / static_cast<float>(kDefaultWindowWidth), kMinAutoTextScale,
                       kMaxAutoTextScale);
}

// RenderRunwayDiagram, sized to fill the card's available width (capped so
// it never outgrows it) and centered with a small margin on each side.
void RenderCenteredRunwayDiagram(const core::Airport* airport, const core::AirportEntry* entry)
{
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float rawMaxDiameter = availableWidth - 2.0f * kUiWindowPaddingX - kWindsockStripWidth;
    // Ternaries, not std::max/std::min -- XPLMUtilities.h pulls in
    // <windows.h> without NOMINMAX here, whose bare max/min macros mangle
    // any std::max/std::min call in this file.
    const float maxDiameter = rawMaxDiameter > 0.0f ? rawMaxDiameter : 0.0f;

    const float diameter = std::clamp(kRunwayDiagramDiameter * kRunwayDiagramSizeMultiplier * ComputeAutoTextScale(),
                                       0.0f, maxDiameter);

    const float totalWidth = diameter + kWindsockStripWidth;
    const float rawLeftOffset = (availableWidth - totalWidth) * 0.5f;
    const float leftOffset = rawLeftOffset > 0.0f ? rawLeftOffset : 0.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + leftOffset);

    RenderRunwayDiagram(airport, entry, diameter);
}

} // namespace

MainWindow::MainWindow(int left, int top, int right, int bottom) : ImgWindow(left, top, right, bottom)
{
    SetWindowTitle("Traffic Runway Monitor");
    SetWindowResizingLimits(kMinWindowWidth, kMinWindowHeight, kMaxWindowWidth, kMaxWindowHeight);

    // Must run after the ImgWindow base constructor above, which is what
    // creates the ImGui context ApplyTheme()'s ImGui::GetStyle() call needs
    // -- calling it any earlier (e.g. from InitFontAtlas(), before any
    // MainWindow exists) dereferences a null context and crashes.
    ApplyTheme();
}

void MainWindow::InitFontAtlas()
{
    char systemPath[512];
    XPLMGetSystemPath(systemPath);
    std::string fontPath(systemPath);
#if IBM
    fontPath += "Resources\\fonts\\DejaVuSans.ttf";
#else
    fontPath += "Resources/fonts/DejaVuSans.ttf";
#endif

    auto atlas = std::make_shared<ImgFontAtlas>();
    atlas->AddFontFromFileTTF(fontPath.c_str(), 16.0f);

    // Icon font (third_party/FontAwesome, bundled with the plugin itself --
    // unlike DejaVuSans.ttf above, borrowed from X-Plane's own install).
    // Merged into the same ImFont via MergeMode -- see Theme.h's kIcon*
    // constants. `static`, not a plain local: ImFontConfig::GlyphRanges is a
    // raw pointer ImGui reads from for the font's entire lifetime ("THE
    // ARRAY DATA NEEDS TO PERSIST AS LONG AS THE FONT IS ALIVE"), which a
    // stack-local vector wouldn't satisfy.
    static const std::vector<ImWchar> kIconGlyphRanges = [] {
        std::vector<ImWchar> ranges;
        ranges.reserve(kIconGlyphCodepoints.size() * 2 + 1);
        for (unsigned short codepoint : kIconGlyphCodepoints) {
            ranges.push_back(codepoint);
            ranges.push_back(codepoint);
        }
        ranges.push_back(0);
        return ranges;
    }();

    // Skipped (icons render as tofu boxes) if the plugin's own path can't
    // be resolved.
    if (std::optional<std::string> iconFontPath = sdk::IconFontPath(); iconFontPath.has_value()) {
        ImFontConfig iconFontConfig;
        iconFontConfig.MergeMode = true;
        atlas->AddFontFromFileTTF(iconFontPath->c_str(), 16.0f, &iconFontConfig, kIconGlyphRanges.data());
    }

    atlas->bindTexture();
    ImgWindow::sFontAtlas = atlas;
}

void MainWindow::buildInterface()
{
    ImGui::SetWindowFontScale(ComputeAutoTextScale());

    if (ImGui::BeginTabBar("##trm_tab_bar", ImGuiTabBarFlags_NoTooltip)) {
        char dashboardLabel[32];
        std::snprintf(dashboardLabel, sizeof(dashboardLabel), "%s Dashboard", kIconDashboardTab);
        if (ImGui::BeginTabItem(dashboardLabel)) {
            RenderDashboardTab();
            ImGui::EndTabItem();
        }
        char flightPlanLabel[32];
        std::snprintf(flightPlanLabel, sizeof(flightPlanLabel), "%s Flight Plan", kIconFlightPlanTab);
        if (ImGui::BeginTabItem(flightPlanLabel)) {
            RenderFlightPlanTab();
            ImGui::EndTabItem();
        }
        char historyLabel[32];
        std::snprintf(historyLabel, sizeof(historyLabel), "%s History", kIconHistoryTab);
        if (ImGui::BeginTabItem(historyLabel)) {
            RenderHistoryTab();
            ImGui::EndTabItem();
        }
        char settingsLabel[32];
        std::snprintf(settingsLabel, sizeof(settingsLabel), "%s Settings", kIconSettingsTab);
        if (ImGui::BeginTabItem(settingsLabel)) {
            RenderSettingsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void MainWindow::RenderDashboardTab()
{
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
    ImGui::TextUnformatted("Key:");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kColorConfirmed);
    ImGui::Text("%s confirmed", kIconConfirmed);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWindEstimate);
    ImGui::Text("%s wind guess (see ?)", kIconWindEstimate);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
    ImGui::Text("%s waiting", kIconWaiting);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    const bool hasPinned = display.pinned_entry.has_value();
    const bool hasNearby = !display.nearby_candidates.empty();

    if (!hasPinned && !hasNearby) {
        ImGui::TextUnformatted("No airports found nearby.");
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
        ImGui::Text("  Nothing within %d nm, and no FMS origin/destination loaded.", settings.search_radius_nm);
        ImGui::PopStyleColor();
    } else {
        if (hasPinned) {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorAccent);
            ImGui::Text("%s %s", kIconDashboardTab,
                        display.pinned_kind == core::PinnedKind::kOrigin ? "ORIGIN" : "DESTINATION");
            ImGui::PopStyleColor();

            CardScope pinnedCard = BeginCard();
            RenderAirportCard(*display.pinned_entry, settings.show_raw_metar, settings.pressure_unit,
                               settings.advisory_display_mode, display.pinned_advisory_text);
            ImGui::Spacing();
            RenderCenteredRunwayDiagram(display.pinned_airport, &*display.pinned_entry);
            EndCard(pinnedCard);
            ImGui::Spacing();

            if (hasNearby) {
                ImGui::Separator();
                ImGui::Spacing();
            }
        }

        if (hasNearby) {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorAccent);
            ImGui::Text("%s NEARBY", kIconNearby);
            ImGui::PopStyleColor();
            if (RenderNearbyAirportSelector(display.nearby_candidates, interaction.selected_nearby_icao) &&
                interaction.selected_nearby_icao.has_value() && interaction.on_nearby_selection_changed) {
                interaction.on_nearby_selection_changed(*interaction.selected_nearby_icao);
            }
            ImGui::Spacing();
            if (display.selected_nearby_entry.has_value()) {
                CardScope nearbyCard = BeginCard();
                RenderAirportCard(*display.selected_nearby_entry, settings.show_raw_metar, settings.pressure_unit,
                                   settings.advisory_display_mode, display.selected_nearby_advisory_text,
                                   /*showHeader=*/false);
                ImGui::Spacing();
                RenderCenteredRunwayDiagram(display.selected_nearby_airport, &*display.selected_nearby_entry);
                EndCard(nearbyCard);
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Tracked Aircraft: %d", display.tracked_aircraft_count);
    ImGui::Text("Last Update: %s UTC", display.last_update_utc.c_str());
}

void MainWindow::RenderFlightPlanTab()
{
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
    ImGui::TextWrapped(
        "Origin and destination feed the pinned airport on the Dashboard tab. Each field mirrors X-Plane's own "
        "FMS flight plan live; once the FMS reports no matching entry, it unlocks for manual entry but keeps "
        "showing its last known value until you edit it or a new flight starts.");
    ImGui::PopStyleColor();
    ImGui::Spacing();

    RenderIcaoOverrideField("Origin", origin_field_state_, display.origin_editable, display.flight_reset_epoch,
                             display.origin_override_epoch, display.origin_icao, display.origin_airport_name,
                             interaction.on_origin_override_changed);
    ImGui::Spacing();
    RenderIcaoOverrideField("Destination", destination_field_state_, display.destination_editable,
                             display.flight_reset_epoch, display.destination_override_epoch, display.destination_icao,
                             display.destination_airport_name, interaction.on_destination_override_changed);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    const bool pilotIdConfigured = !settings.simbrief_pilot_id.empty();
    if (ImGui::Button("Fetch from Simbrief") && pilotIdConfigured &&
        display.simbrief_fetch_status != SimbriefFetchUiStatus::kFetching && interaction.on_simbrief_fetch_requested) {
        interaction.on_simbrief_fetch_requested();
    }
    if (!pilotIdConfigured) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
        ImGui::TextWrapped("Set a Simbrief pilot ID in Settings to enable this.");
        ImGui::PopStyleColor();
    } else if (!display.simbrief_fetch_message.empty()) {
        const unsigned int color = display.simbrief_fetch_status == SimbriefFetchUiStatus::kError ? kColorWindEstimate
                                    : display.simbrief_fetch_status == SimbriefFetchUiStatus::kSuccess
                                        ? kColorConfirmed
                                        : kColorWaiting;
        ImGui::PushStyleColor(ImGuiCol_Text, color);
        ImGui::TextWrapped("%s", display.simbrief_fetch_message.c_str());
        ImGui::PopStyleColor();
    }

    if (display.simbrief_route_text.has_value()) {
        ImGui::Spacing();
        ImGui::TextDisabled("Route");
        ImGui::TextWrapped("%s", display.simbrief_route_text->c_str());
    }
}

void MainWindow::RenderSettingsTab()
{
    RenderSectionHeader(kIconDisplaySection, "Display");
    ImGui::TextUnformatted("Altimeter unit");
    ImGui::SameLine();
    if (ImGui::RadioButton("inHg", settings.pressure_unit == core::PressureUnit::kInHg)) {
        settings.pressure_unit = core::PressureUnit::kInHg;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("hPa", settings.pressure_unit == core::PressureUnit::kHpa)) {
        settings.pressure_unit = core::PressureUnit::kHpa;
    }
    ImGui::Spacing();

    ImGui::TextUnformatted("Airport card display");
    ImGui::SameLine();
    if (ImGui::RadioButton("List", settings.advisory_display_mode == core::AdvisoryDisplayMode::kList)) {
        settings.advisory_display_mode = core::AdvisoryDisplayMode::kList;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Natural language",
                            settings.advisory_display_mode == core::AdvisoryDisplayMode::kNaturalLanguage)) {
        settings.advisory_display_mode = core::AdvisoryDisplayMode::kNaturalLanguage;
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("Both", settings.advisory_display_mode == core::AdvisoryDisplayMode::kBoth)) {
        settings.advisory_display_mode = core::AdvisoryDisplayMode::kBoth;
    }
    ImGui::Spacing();

    RenderSectionHeader(kIconSearchSection, "Search");
    RenderPresetSelector("Search radius", kSearchRadiusPresetsNm, settings.search_radius_nm, "%d nm",
                          &settings.search_radius_nm);
    RenderPresetSelector("Max nearby airports", kMaxAirportsPresets, settings.max_displayed_airports, "%d",
                          &settings.max_displayed_airports);
    RenderPresetSelector("Active window", kActiveWindowPresetsMin, settings.active_window_min, "%d min",
                          &settings.active_window_min);
    ImGui::Spacing();

    RenderSectionHeader(kIconStartupSection, "Startup");
    ImGui::Checkbox("Open window automatically on startup", &settings.auto_open_on_startup);
    ImGui::Spacing();

    RenderSectionHeader(kIconFlightPlanTab, "Simbrief");
    if (std::strcmp(simbrief_pilot_id_buf_, settings.simbrief_pilot_id.c_str()) != 0 && !ImGui::IsAnyItemActive()) {
        std::snprintf(simbrief_pilot_id_buf_, sizeof(simbrief_pilot_id_buf_), "%s",
                      settings.simbrief_pilot_id.c_str());
    }
    if (ImGui::InputText("Pilot ID", simbrief_pilot_id_buf_, sizeof(simbrief_pilot_id_buf_))) {
        settings.simbrief_pilot_id = simbrief_pilot_id_buf_;
    }
    ImGui::Spacing();

    RenderSectionHeader(kIconDebug, "Debug");
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
    ImGui::Checkbox("Show raw METAR (debug)", &settings.show_raw_metar);
    ImGui::Checkbox("Log runway matches to Log.txt (debug)", &settings.debug_log_runway_matches);
    ImGui::PopStyleColor();
}

void MainWindow::RenderHistoryTab()
{
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
    ImGui::Text("Confirmed departures/arrivals in the last %d min, most recent first.",
                settings.active_window_min * 2);
    ImGui::PopStyleColor();
    ImGui::Spacing();

    RenderEventHistory(display.recent_events);
}

} // namespace trm::ui
