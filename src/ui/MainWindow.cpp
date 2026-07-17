#include "ui/MainWindow.h"

#include <cstdarg>
#include <cstdio>
#include <memory>

#include "XPLMUtilities.h"

#include "sdk/Log.h"
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

MainWindow::MainWindow(int left, int top, int right, int bottom) : ImgWindow(left, top, right, bottom)
{
    SetWindowTitle("Traffic Runway Monitor");
    SetWindowResizingLimits(kMinWindowWidth, kMinWindowHeight, kMaxWindowWidth, kMaxWindowHeight);
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
    atlas->bindTexture();
    ImgWindow::sFontAtlas = atlas;
}

void MainWindow::buildInterface()
{
    ImGui::SetWindowFontScale(settings.text_size_scale);

    if (ImGui::BeginTabBar("##trm_tab_bar")) {
        if (ImGui::BeginTabItem("Dashboard")) {
            RenderDashboardTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("History")) {
            RenderHistoryTab();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Settings")) {
            RenderSettingsTab();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

void MainWindow::RenderDashboardTab()
{
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
    ImGui::TextUnformatted("Key: * active   ~ wind guess (see ?)   -- waiting");
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
            ImGui::TextUnformatted(display.pinned_kind == core::PinnedKind::kOrigin ? "ORIGIN" : "DESTINATION");
            RenderAirportCard(*display.pinned_entry, settings.show_raw_metar, settings.pressure_unit);
            ImGui::Spacing();
            RenderRunwayDiagram(display.pinned_airport, &*display.pinned_entry);
            ImGui::Spacing();
            if (hasNearby) {
                ImGui::Separator();
                ImGui::Spacing();
            }
        }

        if (hasNearby) {
            ImGui::TextUnformatted("NEARBY");
            if (RenderNearbyAirportSelector(display.nearby_candidates, interaction.selected_nearby_icao) &&
                interaction.selected_nearby_icao.has_value() && interaction.on_nearby_selection_changed) {
                interaction.on_nearby_selection_changed(*interaction.selected_nearby_icao);
            }
            ImGui::Spacing();
            if (display.selected_nearby_entry.has_value()) {
                RenderAirportCard(*display.selected_nearby_entry, settings.show_raw_metar, settings.pressure_unit,
                                   /*showHeader=*/false);
                ImGui::Spacing();
                RenderRunwayDiagram(display.selected_nearby_airport, &*display.selected_nearby_entry);
            }
        }
    }

    ImGui::Separator();
    ImGui::Text("Tracked Aircraft: %d", display.tracked_aircraft_count);
    ImGui::Text("Last Update: %s UTC", display.last_update_utc.c_str());
}

void MainWindow::RenderSettingsTab()
{
    RenderPresetSelector("Text size", kTextSizePresets, settings.text_size_scale, "%.2fx", &settings.text_size_scale);
    ImGui::Spacing();

    RenderPresetSelector("Search radius", kSearchRadiusPresetsNm, settings.search_radius_nm, "%d nm",
                          &settings.search_radius_nm);
    RenderPresetSelector("Max nearby airports", kMaxAirportsPresets, settings.max_displayed_airports, "%d",
                          &settings.max_displayed_airports);
    RenderPresetSelector("Active window", kActiveWindowPresetsMin, settings.active_window_min, "%d min",
                          &settings.active_window_min);
    ImGui::Spacing();

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

    ImGui::Checkbox("Open window automatically on startup", &settings.auto_open_on_startup);
    ImGui::Spacing();

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
