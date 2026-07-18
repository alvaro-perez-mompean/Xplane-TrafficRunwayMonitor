#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "ImgWindow.h"

#include "core/AdvisoryFormat.h"
#include "core/Aggregator.h"
#include "core/AptDat.h"
#include "core/EventLog.h"
#include "core/Format.h"

// The plugin's single ImGui dashboard window: real tabs (Dashboard/
// Settings/History), airport info as cards, colored status badges, a
// runway diagram, nearby airports as a combo-box selector, and a rolling
// confirmed-events table (History tab).
//
// This class is XPLM/ImGui glue: it
// only renders `display`/`settings`/`interaction` below, which the
// orchestration cycle (Plugin.cpp, ~1Hz) is responsible for keeping
// current before each frame. No core:: computation happens in here.

namespace trm::ui {

// Everything the ~1Hz orchestration cycle resolves, read (not computed)
// by buildInterface(). `pinned_airport`/`selected_nearby_airport` are apt.dat lookups for the
// runway-compass diagram's geometry (headings), which AirportEntry itself
// doesn't carry -- these point into the AirportDatabase loaded once at
// startup, which outlives this window and is never mutated, so the
// pointers stay valid for the plugin's lifetime.
struct DisplayState {
    int tracked_aircraft_count = 0;
    std::string last_update_utc = "--:--:--";

    std::optional<core::AirportEntry> pinned_entry;
    // Natural-language advisory sentence for pinned_entry, resolved
    // alongside it -- see core::ResolveAdvisoryText. Always has a value
    // whenever pinned_entry does; ui::Widgets treats it as optional purely
    // defensively.
    std::optional<core::ResolvedAdvisoryText> pinned_advisory_text;
    std::optional<core::PinnedKind> pinned_kind;
    const core::Airport* pinned_airport = nullptr;

    std::vector<core::NearbyCandidate> nearby_candidates;
    std::optional<core::AirportEntry> selected_nearby_entry;
    // Advisory sentence for selected_nearby_entry -- see pinned_advisory_text above.
    std::optional<core::ResolvedAdvisoryText> selected_nearby_advisory_text;
    const core::Airport* selected_nearby_airport = nullptr;

    // History tab: most-recent-first, already windowed/pruned by the
    // orchestration cycle (Plugin.cpp) -- see core::EventLog.
    std::vector<core::RunwayEventSummary> recent_events;
};

// User-adjustable settings. The orchestration cycle reads
// search_radius_nm/max_displayed_airports/active_window_min live each
// cycle. Text size auto-scales with window width instead of being a
// setting -- see MainWindow::buildInterface.
struct Settings {
    int search_radius_nm = 15;
    int max_displayed_airports = 5;
    int active_window_min = 30;
    bool show_raw_metar = false;
    core::PressureUnit pressure_unit = core::PressureUnit::kInHg;

    // Airport-card display mode: the classic per-runway bullet lines, the
    // natural-language advisory sentence, or both. Defaults to List so
    // this feature doesn't change what an existing user sees unless they
    // opt in.
    core::AdvisoryDisplayMode advisory_display_mode = core::AdvisoryDisplayMode::kList;

    // Whether the dashboard window should show itself on XPluginEnable
    // (every X-Plane launch, and every plugin re-enable). Off by default --
    // the window stays closed until opened from the Plugins menu.
    bool auto_open_on_startup = false;

    // When enabled, the orchestration cycle (Plugin.cpp) writes an
    // "RWYDBG"/"RWYCONFIRM"-prefixed line to X-Plane's Log.txt for every
    // runway match and every confirmed sighting respectively -- off by
    // default since it's high-volume, meant for diagnosing runway-matching
    // issues rather than everyday use.
    bool debug_log_runway_matches = false;
};

// UI-only interaction state (not analysis data). A real ImGui tab bar
// means active-tab tracking is unnecessary here (ImGui owns tab
// selection itself).
struct InteractionState {
    std::optional<std::string> selected_nearby_icao;

    // Fired synchronously from buildInterface() the moment the user picks a
    // different nearby airport, so the orchestration cycle (Plugin.cpp) can
    // resolve that one airport's entry immediately instead of leaving
    // display.selected_nearby_entry stale until the next ~1Hz
    // RunAnalysisCycle tick.
    std::function<void(const std::string&)> on_nearby_selection_changed;
};

class MainWindow : public ImgWindow {
public:
    MainWindow(int left, int top, int right, int bottom);

    // Loads DejaVuSans.ttf (ships with X-Plane itself, via
    // XPLMGetSystemPath -- no font bundling needed) into ImgWindow's shared
    // font atlas. Must be called once, before any MainWindow is
    // constructed.
    static void InitFontAtlas();

    // Re-checks the VR-enabled dataref and, if the headset was donned/doffed
    // since the last check, moves the window into/out of the VR layer.
    // ImgWindow otherwise only does this from SetVisible() (see its own
    // moveForVR()), which we call exactly once at startup -- so without a
    // periodic re-check here, a window already visible on the desktop never
    // follows the user into a headset put on mid-session. Cheap to call every
    // ~1Hz cycle (one dataref read, positioning-mode call only on an actual
    // VR-state change).
    void SyncVRPositioning() { moveForVR(); }

    DisplayState display;
    Settings settings;
    InteractionState interaction;

protected:
    void buildInterface() override;

private:
    void RenderDashboardTab();
    void RenderSettingsTab();
    void RenderHistoryTab();
};

} // namespace trm::ui
