#pragma once

#include <optional>

// Persists the plugin's user-adjustable settings as a
// flat "key=value" text file, so they survive across X-Plane restarts
// instead of resetting to ui::Settings's hardcoded defaults every load.
// Stored next to X-Plane's own prefs file (XPLMGetPrefsPath's directory),
// named TrafficRunwayMonitor.prf -- a plugin's own file, not X-Plane.prf
// itself, which XPLMGetPrefsPath's returned path actually names.
//
// A plain mirror of ui::Settings's fields rather than a dependency on
// ui::Settings itself: sdk/ has no business depending on ui/ (the reverse
// of every other dependency in this codebase), so Plugin.cpp -- which
// already depends on both -- is responsible for copying fields across.
//
// Real filesystem + XPLMGetPrefsPath call -- thin glue, not unit-tested,
// same as the rest of sdk/.

namespace trm::sdk {

struct PersistedSettings {
    int search_radius_nm = 15;
    int max_displayed_airports = 5;
    int active_window_min = 30;
    float text_size_scale = 1.0f;
    bool show_raw_metar = false;
    bool debug_log_runway_matches = false;
    int pressure_unit = 0; // 0 = inHg, 1 = hPa -- mirrors core::PressureUnit's ordering
};

void SaveSettings(const PersistedSettings& settings);

// nullopt if the settings file doesn't exist yet (first run) -- callers
// should keep ui::Settings's own defaults in that case.
std::optional<PersistedSettings> LoadSettings();

} // namespace trm::sdk
