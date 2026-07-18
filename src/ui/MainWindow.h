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
#include "core/SimbriefOfp.h"
#include "ui/Widgets.h"

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

// Outcome of the last (or in-flight) Simbrief OFP fetch -- a ui-local
// mirror of sdk::SimbriefFetchStatus, translated by Plugin.cpp each cycle
// so ui:: doesn't need to depend on sdk:: at the header level (same idea as
// ui::Settings::pressure_unit mirroring sdk::PersistedSettings' plain int).
enum class SimbriefFetchUiStatus { kIdle, kFetching, kSuccess, kError };

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

    // Flight Plan tab: whether the origin/destination ICAO field is
    // currently editable, i.e. the native FMS has no matching entry this
    // cycle (see sdk::FmsOriginDestination). Set every cycle in
    // Plugin.cpp's RunAnalysisCycle from sdk::FmsOriginDestination's
    // origin_fresh/destination_fresh. Independent per field.
    bool origin_editable = false;
    bool destination_editable = false;
    // The effective (pinned) ICAO for each field -- the live source value
    // while locked, or the last-known value (previous source read, or a
    // manual entry) once stale, sticky until the user edits it or a new
    // flight starts. Plugin.cpp resolves which; ui:: never computes it,
    // only mirrors it into the field -- see ui::RenderIcaoOverrideField.
    std::optional<std::string> origin_icao;
    std::optional<std::string> destination_icao;
    // Airport name for origin_icao/destination_icao if it resolves in
    // g_airportDatabase, nullopt otherwise (rendered as an "unknown ICAO"
    // warning -- lets the user confirm a typed ICAO is a real airport).
    std::optional<std::string> origin_airport_name;
    std::optional<std::string> destination_airport_name;
    // Bumped by Plugin.cpp (see g_flightResetEpoch) exactly when a new
    // flight starts and the pinned origin/destination get cleared --
    // RenderIcaoOverrideField compares this against its own last-seen copy
    // to force its InputText buffer to follow even when a field's editable
    // state doesn't itself change across the reset.
    int flight_reset_epoch = 0;
    // Bumped by Plugin.cpp whenever origin_icao/destination_icao's pinned
    // value is set by something other than the user typing into this same
    // field -- currently only a successful Simbrief fetch. Same
    // force-resync purpose as flight_reset_epoch, just for a value change
    // that isn't a flight reset (see RenderIcaoOverrideField). Independent
    // per field since a fetch can fill one without the other.
    int origin_override_epoch = 0;
    int destination_override_epoch = 0;

    // Flight Plan tab: resolved every cycle in Plugin.cpp from
    // sdk::SimbriefClient::Poll() -- kIdle until the user has pressed
    // "Fetch from Simbrief" this session.
    SimbriefFetchUiStatus simbrief_fetch_status = SimbriefFetchUiStatus::kIdle;
    // Human-readable detail: e.g. "Loaded KJFK -> KLAX" on kSuccess, the
    // error text on kError, "Fetching..." on kFetching, empty on kIdle.
    // Auto-cleared a few seconds after a success (see Plugin.cpp's
    // kSimbriefSuccessMessageTtlSec) so it reads as a toast, not a
    // permanent status line.
    std::string simbrief_fetch_message;
    // LIDO-style route line from the last successful fetch (see
    // core::SimbriefOriginDestination::route_text) -- unlike
    // simbrief_fetch_message above, this is NOT time-limited: it stays
    // displayed until the next fetch or a new flight (nullopt), since it's
    // reference text the user may want to keep reading, not a transient
    // confirmation toast.
    std::optional<std::string> simbrief_route_text;
    // LIDO-style fuel figures from the last successful fetch (see
    // core::SimbriefFuelPlan) -- same not-time-limited lifetime as
    // simbrief_route_text above, default-constructed (all fields nullopt,
    // renders nothing) until the first fetch.
    core::SimbriefFuelPlan simbrief_fuel;
    // LIDO-style weight figures from the last successful fetch (see
    // core::SimbriefWeights) -- same lifetime/default convention as
    // simbrief_fuel above.
    core::SimbriefWeights simbrief_weights;
    // Header/identity figures from the last successful fetch (see
    // core::SimbriefHeader) -- same lifetime/default convention as
    // simbrief_fuel above.
    core::SimbriefHeader simbrief_header;
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

    // Simbrief "pilot ID" used by the Flight Plan tab's "Fetch from
    // Simbrief" button (api/xml.fetcher.php?userid=<id>&json=1). Stored/
    // rendered as text even though it's numeric -- no int parsing needed.
    // Empty until the user sets it; persisted like every other field here.
    std::string simbrief_pilot_id;
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

    // Flight Plan tab: fired the moment the user edits an unlocked
    // origin/destination field (RenderIcaoOverrideField, Widgets.cpp).
    // Plugin.cpp stores the typed value as g_originOverride/
    // g_destinationOverride, which the next RunAnalysisCycle tick applies
    // in place of the (stale) source value -- see DisplayState::
    // origin_editable/destination_editable above.
    std::function<void(const std::string&)> on_origin_override_changed;
    std::function<void(const std::string&)> on_destination_override_changed;

    // Flight Plan tab: fired the moment the user presses "Fetch from
    // Simbrief". Plugin.cpp starts the async fetch
    // (sdk::SimbriefClient::RequestFetch) -- ui:: does no I/O itself, per
    // CLAUDE.md's ui/ layering rule.
    std::function<void()> on_simbrief_fetch_requested;
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
    void RenderFlightPlanTab();
    void RenderSettingsTab();
    void RenderHistoryTab();

    // Caller-owned persistent state for the Flight Plan tab's two
    // RenderIcaoOverrideField call sites (see IcaoOverrideFieldState,
    // Widgets.h) -- ImGui is immediate-mode and doesn't own text-editing
    // state itself, and a plain function-local static wouldn't distinguish
    // the origin call site from the destination one.
    IcaoOverrideFieldState origin_field_state_;
    IcaoOverrideFieldState destination_field_state_;

    // Settings tab's Simbrief Pilot ID field: ImGui::InputText needs a raw
    // char buffer, not std::string, so this mirrors settings.simbrief_pilot_id
    // while being edited (synced in on activation, written back on edit --
    // same idea as IcaoOverrideFieldState::buf above).
    char simbrief_pilot_id_buf_[32] = "";
};

} // namespace trm::ui
