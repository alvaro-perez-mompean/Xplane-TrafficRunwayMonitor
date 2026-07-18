// TrafficRunwayMonitor -- native plugin entry point.
//
// Wires every module into the real ~1Hz analysis cycle.
//
// Two traffic sources feed the same downstream pipeline
// (TrendFilter/PhaseClassifier/RunwayMatcher/SightingTracker): sdk::LtapiSource
// (LiveTraffic's own bulk data, not capped at 63 aircraft) is preferred
// whenever it's available this cycle; sdk::TcasSource (TCAS Override /
// legacy multiplayer, hard-capped at 63) is the fallback. Each source gets
// its own SlotAnalysisState array -- deliberately NOT shared -- because a
// "slot index" means a completely different thing between the two
// (LTAPI's is a stable-assigned key, TCAS's is a raw array index); sharing
// state across a mid-session source switch would silently misattribute
// one real aircraft's trend/sighting history to a different one.

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMMenus.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include "core/AdvisoryFormat.h"
#include "core/Aggregator.h"
#include "core/AptDat.h"
#include "core/EventLog.h"
#include "core/PhaseClassifier.h"
#include "core/RunwayMatcher.h"
#include "core/SightingTracker.h"
#include "core/TrendFilter.h"
#include "core/WindEstimate.h"

#include "sdk/AptDatLoader.h"
#include "sdk/FmsOrigin.h"
#include "sdk/Log.h"
#include "sdk/LtapiSource.h"
#include "sdk/SettingsStore.h"
#include "sdk/TcasSource.h"
#include "sdk/Weather.h"

#include "ui/MainWindow.h"
#include "ui/Theme.h"

using namespace trm;

namespace {

constexpr float kCycleIntervalSec = 1.0f;

// Per-slot bookkeeping the orchestration cycle owns across cycles that
// isn't already owned by sdk::TcasSource (stale-slot validity) internally:
// the trend-filter hysteresis (TrendFilter) and the confirm-before/
// confirm-after sighting state (SightingTracker).
struct SlotAnalysisState {
    bool has_prev_gs = false;
    double prev_gs_kt = 0.0;
    core::GsTrendState gs_trend_state;
    core::VsStateFilterState vs_state_state;
    core::SlotSightingState sighting_state;
};

core::AirportDatabase g_airportDatabase;
core::AggregatorConfig g_aggregatorConfig;
core::SightingTracker g_sightingTracker;
core::EventLog g_eventLog;

// History tab window: 2x the user-adjustable active window
// (settings.active_window_min), so it scales with whatever recency the
// user already cares about rather than a fixed value of its own.
constexpr double kEventHistoryWindowMultiplier = 2.0;

std::unique_ptr<sdk::TcasSource> g_tcasSource;
std::unique_ptr<sdk::LtapiSource> g_ltapiSource;
sdk::FmsOrigin g_fmsOrigin;
sdk::Weather g_weather;

// index 0 unused in both, matching TcasSource/LtapiSource's own slot convention.
std::vector<SlotAnalysisState> g_tcasSlotStates;
std::vector<SlotAnalysisState> g_ltapiSlotStates;

std::unique_ptr<ui::MainWindow> g_mainWindow;

XPLMDataRef g_latitudeRef = nullptr;
XPLMDataRef g_longitudeRef = nullptr;

// Plugins-menu entry (created in XPluginEnable, torn down in XPluginDisable
// so a disable/re-enable cycle -- e.g. via Plugin Admin -- doesn't pile up
// duplicate entries) -- the only way to get the dashboard back once its
// built-in close button has been clicked, since ImgWindow's close button
// hides the window directly via XPLM rather than calling back into
// MainWindow::SetVisible. A checkable toggle rather than a one-way "Show"
// action, so the menu itself doubles as a visibility indicator -- checked
// whenever the window is currently shown, kept in sync every cycle
// (SyncMenuCheckmark) since the window's own close button changes
// visibility without going through this handler at all.
XPLMMenuID g_pluginMenu = nullptr;
int g_pluginMenuContainerItem = -1;
int g_menuShowItemIndex = -1;

void SyncMenuCheckmark()
{
    if (!g_mainWindow || !g_pluginMenu || g_menuShowItemIndex < 0) {
        return;
    }
    XPLMCheckMenuItem(g_pluginMenu, g_menuShowItemIndex,
                       g_mainWindow->GetVisible() ? xplm_Menu_Checked : xplm_Menu_Unchecked);
}

void MenuHandler(void* /*inMenuRef*/, void* /*inItemRef*/)
{
    if (!g_mainWindow) {
        return;
    }
    const bool showNow = !g_mainWindow->GetVisible();
    g_mainWindow->SetVisible(showNow);
    if (showNow) {
        g_mainWindow->BringWindowToFront();
    }
    SyncMenuCheckmark();
}

const core::Airport* FindAirport(const core::AirportDatabase& db, const std::string& icao)
{
    const auto it = db.find(icao);
    return it != db.end() ? &it->second : nullptr;
}

// Human-readable phase name for the RWYDBG debug log line.
const char* PhaseDebugName(core::FlightPhase phase)
{
    switch (phase) {
        case core::FlightPhase::kTaxi: return "taxi";
        case core::FlightPhase::kTakeoffRoll: return "takeoff_roll";
        case core::FlightPhase::kInitialClimb: return "initial_climb";
        case core::FlightPhase::kDeparting: return "departing";
        case core::FlightPhase::kArriving: return "arriving";
        case core::FlightPhase::kFinalApproach: return "final_approach";
        case core::FlightPhase::kLandingRollout: return "landing_rollout";
        case core::FlightPhase::kAirborneEnroute: return "airborne_enroute";
    }
    return "?";
}

std::string FormatUtcNow()
{
    const std::time_t t = std::time(nullptr);
    std::tm utc{};
#if IBM
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d", utc.tm_hour, utc.tm_min, utc.tm_sec);
    return buf;
}

// Already-resolved wind/METAR inputs for one airport (Aggregator can't
// fetch these itself). Airport-position wind is queried directly
// when the airport has a reference point (always succeeds per
// sdk::Weather::QueryWindAt's own contract), else the region-array
// fallback is consulted instead, matching core::EstimateWindFavoredRunwayEnd's
// documented resolution order.
core::AirportEntryInputs ResolveAirportInputs(const std::string& icao, const core::Airport* airport)
{
    core::AirportEntryInputs inputs;
    if (airport != nullptr && airport->HasReferencePoint()) {
        inputs.wind_airport_position_reading =
            g_weather.QueryWindAt(airport->ref_lat_deg, airport->ref_lon_deg, airport->elevation_ft * 0.3048);
    }
    if (!inputs.wind_airport_position_reading.has_value()) {
        inputs.wind_aircraft_position_reading = g_weather.ReadRegionWindFallback();
    }
    inputs.metar = g_weather.GetMetarFor(icao);
    return inputs;
}

// Traffic collection and sighting-tracking run inline in one loop
// iteration, since everything needed is known within that iteration.
// Returns the count of currently-valid tracked aircraft. `readings`/`slotStates`
// come from whichever traffic source is active this cycle (see
// RunAnalysisCycle) -- this function itself is source-agnostic.
int CollectAndTrackTraffic(const core::AirportDatabase& db, const std::vector<core::NearbyAirport>& nearest,
                            const std::vector<std::string>& extraCandidates, double nowSec,
                            const std::vector<sdk::SlotReading>& readings, std::vector<SlotAnalysisState>& slotStates,
                            bool debugLogRunwayMatches)
{
    int trackedCount = 0;

    for (int i = 1; i < static_cast<int>(readings.size()); ++i) {
        const sdk::SlotReading& reading = readings[static_cast<size_t>(i)];
        SlotAnalysisState& state = slotStates[static_cast<size_t>(i)];

        if (!reading.valid) {
            // Also drops ground_sighting/pending_arrival (SightingTracker's
            // own confirm-before/confirm-after state): this slot index can
            // be recycled for a completely different real aircraft shortly
            // after this one despawns.
            state.has_prev_gs = false;
            g_sightingTracker.ClearSlotState(state.sighting_state);
            continue;
        }

        ++trackedCount;

        if (reading.is_helicopter) {
            // Runway matching/phase classification assume fixed-wing
            // traffic-pattern behavior (heading-aligned approach,
            // decelerating through touchdown) -- a helicopter can hover,
            // approach from any angle, and settle to zero groundspeed off
            // to the side of a runway without ever being "aligned" the way
            // this pipeline expects, or coincidentally sit inside the
            // matching tolerance while landing somewhere else nearby. Still
            // counted as tracked traffic, just excluded from the runway
            // arrival/departure pipeline entirely. See sdk::SlotReading's
            // own comment: only ever true when sourced from LTAPI.
            state.has_prev_gs = false;
            g_sightingTracker.ClearSlotState(state.sighting_state);
            continue;
        }

        // Trend uses *last cycle's* gs_kt, so this runs before it's overwritten.
        const core::GsTrend gsTrend =
            core::UpdateGsTrend(state.gs_trend_state, state.has_prev_gs, state.prev_gs_kt, reading.gs_kt);
        const core::VsState vsState = core::UpdateVsState(state.vs_state_state, reading.vs_mps);
        state.has_prev_gs = true;
        state.prev_gs_kt = reading.gs_kt;

        const core::RunwayEnd* matchedRunway = nullptr;
        const core::Airport* matchedAirport = nullptr;
        std::string matchedIcao;

        auto tryMatch = [&](const std::string& icao) {
            const core::Airport* airport = FindAirport(db, icao);
            if (airport == nullptr) {
                return false;
            }
            const core::RunwayEnd* end =
                core::MatchRunwayEnd(*airport, reading.lat_deg, reading.lon_deg, reading.heading_true_deg);
            if (end != nullptr) {
                matchedRunway = end;
                matchedAirport = airport;
                matchedIcao = icao;
                return true;
            }
            return false;
        };

        for (const auto& candidate : nearest) {
            if (tryMatch(candidate.icao)) {
                break;
            }
        }
        if (matchedRunway == nullptr) {
            for (const auto& icao : extraCandidates) {
                if (tryMatch(icao)) {
                    break;
                }
            }
        }

        // For AGL, fall back to the nearest airport's elevation even when
        // unmatched (e.g. still en route) as the best reference available.
        const core::Airport* refAirport = matchedAirport;
        if (refAirport == nullptr && !nearest.empty()) {
            refAirport = FindAirport(db, nearest.front().icao);
        }

        // With no reference elevation available, phase is left unresolved
        // for this cycle. kAirborneEnroute is a safe stand-in --
        // SightingTracker::ProcessSlot never acts on it either (only
        // kTaxi/kTakeoffRoll/kInitialClimb/kFinalApproach/kLandingRollout do).
        core::FlightPhase phase = core::FlightPhase::kAirborneEnroute;
        double aglM = 0.0;
        bool haveAgl = false;
        if (refAirport != nullptr) {
            aglM = reading.msl_m - refAirport->elevation_ft * 0.3048;
            haveAgl = true;
            phase = core::ClassifyPhase(aglM, reading.gs_kt, vsState, matchedRunway != nullptr, gsTrend);
        }

        // Diagnostic logging, gated behind Settings tab's "Log runway
        // matches to Log.txt (debug)" checkbox -- fires whenever a runway
        // match happens this cycle, so the lead-up to a confirmed sighting
        // (not just the confirmation itself) is visible in Log.txt. Grep
        // for "RWYDBG".
        if (debugLogRunwayMatches && matchedRunway != nullptr && haveAgl) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "RWYDBG slot=%d icao=%s rwy=%s phase=%s agl_m=%.1f gs_kt=%.1f gsTrend=%d "
                          "lat=%.5f lon=%.5f hdg=%.1f",
                          i, matchedIcao.c_str(), matchedRunway->id.c_str(), PhaseDebugName(phase), aglM,
                          reading.gs_kt, static_cast<int>(gsTrend), reading.lat_deg, reading.lon_deg,
                          reading.heading_true_deg);
            sdk::Log(sdk::LogLevel::Info, buf);
        }

        core::SightingTracker::SlotObservation observation;
        observation.icao = matchedIcao;
        observation.runway_id = (matchedRunway != nullptr) ? matchedRunway->id : std::string();
        observation.other_end_id = (matchedRunway != nullptr) ? matchedRunway->other_end_id : std::string();
        observation.phase = phase;
        observation.callsign = reading.callsign;
        if (std::optional<core::RunwayEvent> event =
                g_sightingTracker.ProcessSlot(i, state.sighting_state, observation, nowSec)) {
            g_eventLog.Record(*event);

            // Diagnostic logging, gated behind the same debug checkbox as
            // RWYDBG above -- marks the exact cycle a sighting was newly
            // confirmed (i.e. what actually landed in the History tab), so
            // a log review doesn't have to reconstruct it from the raw
            // per-cycle match trace. Grep for "RWYCONFIRM".
            if (debugLogRunwayMatches) {
                char confirmBuf[192];
                std::snprintf(confirmBuf, sizeof(confirmBuf),
                              "RWYCONFIRM slot=%d icao=%s rwy=%s category=%s callsign=%s",
                              i, event->icao.c_str(), event->runway_id.c_str(),
                              event->category == core::SightingCategory::kArrival ? "arrival" : "departure",
                              event->callsign.empty() ? "?" : event->callsign.c_str());
                sdk::Log(sdk::LogLevel::Info, confirmBuf);
            }
        }
    }

    return trackedCount;
}

// core::BuildPinnedEntry itself isn't used here: it needs already-resolved
// wind/METAR inputs for whichever airport ends up selected, so the
// selection (core::SelectPinnedAirport) has to run first anyway -- calling
// core::BuildAirportEntry directly with the correctly-resolved inputs
// avoids running the same selection twice.
void UpdatePinnedEntry(const core::AirportDatabase& db, double lat, double lon,
                       const sdk::FmsOriginDestination& fms, double nowSec)
{
    std::optional<double> originDistanceNm;
    if (fms.origin_icao.has_value()) {
        originDistanceNm = core::AirportDistanceNm(db, *fms.origin_icao, lat, lon);
    }

    const auto selection = core::SelectPinnedAirport(fms.origin_icao, fms.destination_icao, originDistanceNm,
                                                       g_aggregatorConfig.origin_pin_radius_nm);
    if (!selection.has_value()) {
        g_mainWindow->display.pinned_entry.reset();
        g_mainWindow->display.pinned_advisory_text.reset();
        g_mainWindow->display.pinned_kind.reset();
        g_mainWindow->display.pinned_airport = nullptr;
        return;
    }

    const core::Airport* airport = FindAirport(db, selection->icao);
    const std::optional<double> distanceNm = core::AirportDistanceNm(db, selection->icao, lat, lon);
    const core::AirportEntryInputs inputs = ResolveAirportInputs(selection->icao, airport);

    g_mainWindow->display.pinned_entry = core::BuildAirportEntry(selection->icao, distanceNm, airport,
                                                                   g_sightingTracker, inputs, nowSec, g_aggregatorConfig);
    g_mainWindow->display.pinned_advisory_text = core::ResolveAdvisoryText(
        *g_mainWindow->display.pinned_entry, g_mainWindow->settings.pressure_unit, airport);
    g_mainWindow->display.pinned_kind = selection->kind;
    g_mainWindow->display.pinned_airport = airport;
}

// Resolves and stores the full display entry for whichever airport is
// currently selected in the "NEARBY" combo -- the expensive per-airport work
// (wind estimate, METAR) only ever runs for that one airport, not every
// candidate. Shared by the 1Hz UpdateNearbyCandidates cycle below and by
// HandleNearbySelectionChanged, which calls this the instant the user picks
// a different airport rather than waiting for the next cycle tick.
void ResolveSelectedNearbyEntry(const core::AirportDatabase& db, const std::string& icao,
                                 std::optional<double> distanceNm, double nowSec)
{
    const core::Airport* airport = FindAirport(db, icao);
    const core::AirportEntryInputs inputs = ResolveAirportInputs(icao, airport);
    g_mainWindow->display.selected_nearby_entry =
        core::BuildAirportEntry(icao, distanceNm, airport, g_sightingTracker, inputs, nowSec, g_aggregatorConfig);
    g_mainWindow->display.selected_nearby_advisory_text = core::ResolveAdvisoryText(
        *g_mainWindow->display.selected_nearby_entry, g_mainWindow->settings.pressure_unit, airport);
    g_mainWindow->display.selected_nearby_airport = airport;
}

// Keeps the current selection if it's still a valid candidate, else falls
// back to nearest, then resolves the single selected candidate's full entry
// via ResolveSelectedNearbyEntry.
void UpdateNearbyCandidates(const core::AirportDatabase& db, const std::vector<core::NearbyAirport>& nearest,
                             double nowSec)
{
    std::optional<std::string> pinnedIcao;
    if (g_mainWindow->display.pinned_entry.has_value()) {
        pinnedIcao = g_mainWindow->display.pinned_entry->icao;
    }

    ui::DisplayState& display = g_mainWindow->display;
    ui::InteractionState& interaction = g_mainWindow->interaction;

    display.nearby_candidates = core::BuildNearbyCandidates(nearest, pinnedIcao, g_mainWindow->settings.max_displayed_airports);

    bool selectionStillValid = false;
    for (const auto& candidate : display.nearby_candidates) {
        if (interaction.selected_nearby_icao.has_value() && candidate.icao == *interaction.selected_nearby_icao) {
            selectionStillValid = true;
            break;
        }
    }
    if (!selectionStillValid) {
        interaction.selected_nearby_icao = display.nearby_candidates.empty()
                                                ? std::nullopt
                                                : std::optional<std::string>(display.nearby_candidates.front().icao);
    }

    display.selected_nearby_entry.reset();
    display.selected_nearby_advisory_text.reset();
    display.selected_nearby_airport = nullptr;
    for (const auto& candidate : display.nearby_candidates) {
        if (interaction.selected_nearby_icao.has_value() && candidate.icao == *interaction.selected_nearby_icao) {
            ResolveSelectedNearbyEntry(db, candidate.icao, candidate.distance_nm, nowSec);
            break;
        }
    }
}

// Fired synchronously from MainWindow's on_nearby_selection_changed hook the
// moment the user picks a different airport in the "NEARBY" combo (see
// MainWindow.h's InteractionState) -- resolves that one airport's entry
// immediately, on the same frame, instead of leaving display.selected_nearby_entry
// showing the previous selection until the next ~1Hz RunAnalysisCycle tick.
void HandleNearbySelectionChanged(const std::string& icao)
{
    const double lat = (g_latitudeRef != nullptr) ? XPLMGetDatad(g_latitudeRef) : 0.0;
    const double lon = (g_longitudeRef != nullptr) ? XPLMGetDatad(g_longitudeRef) : 0.0;
    const std::optional<double> distanceNm = core::AirportDistanceNm(g_airportDatabase, icao, lat, lon);
    ResolveSelectedNearbyEntry(g_airportDatabase, icao, distanceNm, XPLMGetElapsedTime());
}

// The ~1Hz tick that resolves every input and rebuilds the window's
// display state.
void RunAnalysisCycle()
{
    if (!g_mainWindow || !g_tcasSource) {
        return;
    }

    const double nowSec = XPLMGetElapsedTime();
    const double lat = (g_latitudeRef != nullptr) ? XPLMGetDatad(g_latitudeRef) : 0.0;
    const double lon = (g_longitudeRef != nullptr) ? XPLMGetDatad(g_longitudeRef) : 0.0;

    const std::vector<core::NearbyAirport> nearest = core::FindNearestAirports(
        g_airportDatabase, lat, lon, static_cast<double>(g_mainWindow->settings.search_radius_nm));

    const sdk::FmsOriginDestination fms = g_fmsOrigin.Resolve();

    // Extra candidates for runway matching only (not for display): an
    // FMS origin/destination far outside the search radius should still be
    // matchable against.
    std::vector<std::string> extraCandidates;
    auto alreadyNearby = [&](const std::string& icao) {
        for (const auto& airport : nearest) {
            if (airport.icao == icao) {
                return true;
            }
        }
        return false;
    };
    if (fms.origin_icao.has_value() && !alreadyNearby(*fms.origin_icao)) {
        extraCandidates.push_back(*fms.origin_icao);
    }
    if (fms.destination_icao.has_value() && !alreadyNearby(*fms.destination_icao)) {
        extraCandidates.push_back(*fms.destination_icao);
    }

    // Prefer LTAPI whenever it's actively displaying aircraft this cycle
    // (checked fresh every call: even when opted in, only takes over once
    // proven available and sane for that specific cycle); otherwise fall
    // back to TCAS Override / legacy multiplayer. Each source keeps its
    // own per-slot state, so a
    // mid-session switch never confuses one real aircraft's trend/sighting
    // history for another's (see this file's own top-of-file comment).
    int trackedCount = 0;
    const bool debugLogRunwayMatches = g_mainWindow->settings.debug_log_runway_matches;
    if (g_ltapiSource && g_ltapiSource->IsAvailable()) {
        const std::vector<sdk::SlotReading> readings = g_ltapiSource->CollectTraffic();
        trackedCount = CollectAndTrackTraffic(g_airportDatabase, nearest, extraCandidates, nowSec, readings,
                                               g_ltapiSlotStates, debugLogRunwayMatches);
    } else if (g_tcasSource) {
        const std::vector<sdk::SlotReading> readings = g_tcasSource->CollectTraffic(nowSec);
        trackedCount = CollectAndTrackTraffic(g_airportDatabase, nearest, extraCandidates, nowSec, readings,
                                               g_tcasSlotStates, debugLogRunwayMatches);
    }

    // The active window is user-adjustable at runtime, so it's read fresh
    // every cycle.
    g_aggregatorConfig.active_window_sec = g_mainWindow->settings.active_window_min * 60.0;
    g_sightingTracker.PruneStaleSightings(
        nowSec, g_aggregatorConfig.active_window_sec * g_aggregatorConfig.history_window_multiplier);

    g_eventLog.PruneOlderThan(nowSec, g_aggregatorConfig.active_window_sec * kEventHistoryWindowMultiplier);
    g_mainWindow->display.recent_events = g_eventLog.Summaries(nowSec);

    UpdatePinnedEntry(g_airportDatabase, lat, lon, fms, nowSec);
    UpdateNearbyCandidates(g_airportDatabase, nearest, nowSec);

    g_mainWindow->display.tracked_aircraft_count = trackedCount;
    g_mainWindow->display.last_update_utc = FormatUtcNow();
}

float FlightLoopCallback(float, float, int, void*)
{
    RunAnalysisCycle();
    if (g_mainWindow) {
        g_mainWindow->SyncVRPositioning();
    }
    SyncMenuCheckmark();
    return kCycleIntervalSec;
}

} // namespace

PLUGIN_API int XPluginStart(char* outName, char* outSig, char* outDesc)
{
    std::strcpy(outName, "Traffic Runway Monitor");
    std::strcpy(outSig, "trm.traffic_runway_monitor");
    std::strcpy(outDesc, "Live nearby-airport active/history runway detection from traffic, wind, and FMS data.");

    g_latitudeRef = XPLMFindDataRef("sim/flightmodel/position/latitude");
    g_longitudeRef = XPLMFindDataRef("sim/flightmodel/position/longitude");

    return 1;
}

PLUGIN_API void XPluginStop(void) {}

PLUGIN_API int XPluginEnable(void)
{
    if (std::optional<core::AirportDatabase> db = sdk::LoadMergedAptDat()) {
        g_airportDatabase = std::move(*db);
    } else {
        sdk::Log(sdk::LogLevel::Error, "could not open the default apt.dat (tried both the XP12 and XP11 paths).");
    }

    g_tcasSource = std::make_unique<sdk::TcasSource>();
    g_tcasSlotStates.assign(static_cast<size_t>(g_tcasSource->SlotCount()) + 1, SlotAnalysisState{});

    g_ltapiSource = std::make_unique<sdk::LtapiSource>();
    g_ltapiSlotStates.assign(static_cast<size_t>(g_ltapiSource->MaxSlots()) + 1, SlotAnalysisState{});

    ui::MainWindow::InitFontAtlas();

    int screenLeft = 0, screenTop = 0, screenRight = 0, screenBottom = 0;
    XPLMGetScreenBoundsGlobal(&screenLeft, &screenTop, &screenRight, &screenBottom);
    const int left = screenLeft + 80;
    const int top = screenTop - 80;
    g_mainWindow = std::make_unique<ui::MainWindow>(left, top, left + ui::kDefaultWindowWidth,
                                                      top - ui::kDefaultWindowHeight);
    g_mainWindow->interaction.on_nearby_selection_changed = HandleNearbySelectionChanged;

    if (std::optional<sdk::PersistedSettings> persisted = sdk::LoadSettings()) {
        ui::Settings& settings = g_mainWindow->settings;
        settings.search_radius_nm = persisted->search_radius_nm;
        settings.max_displayed_airports = persisted->max_displayed_airports;
        settings.active_window_min = persisted->active_window_min;
        settings.show_raw_metar = persisted->show_raw_metar;
        settings.debug_log_runway_matches = persisted->debug_log_runway_matches;
        settings.pressure_unit =
            persisted->pressure_unit == 1 ? core::PressureUnit::kHpa : core::PressureUnit::kInHg;
        settings.auto_open_on_startup = persisted->auto_open_on_startup;
        switch (persisted->advisory_display_mode) {
            case 1:
                settings.advisory_display_mode = core::AdvisoryDisplayMode::kNaturalLanguage;
                break;
            case 2:
                settings.advisory_display_mode = core::AdvisoryDisplayMode::kBoth;
                break;
            default:
                settings.advisory_display_mode = core::AdvisoryDisplayMode::kList;
                break;
        }
    }

    g_mainWindow->SetVisible(g_mainWindow->settings.auto_open_on_startup);

    g_pluginMenuContainerItem = XPLMAppendMenuItem(XPLMFindPluginsMenu(), "Traffic Runway Monitor", nullptr, 0);
    g_pluginMenu = XPLMCreateMenu("Traffic Runway Monitor", XPLMFindPluginsMenu(), g_pluginMenuContainerItem,
                                   MenuHandler, nullptr);
    g_menuShowItemIndex = XPLMAppendMenuItem(g_pluginMenu, "Show Window", nullptr, 0);
    SyncMenuCheckmark();

    XPLMRegisterFlightLoopCallback(FlightLoopCallback, kCycleIntervalSec, nullptr);
    return 1;
}

PLUGIN_API void XPluginDisable(void)
{
    XPLMUnregisterFlightLoopCallback(FlightLoopCallback, nullptr);

    if (g_mainWindow) {
        const ui::Settings& settings = g_mainWindow->settings;
        sdk::PersistedSettings persisted;
        persisted.search_radius_nm = settings.search_radius_nm;
        persisted.max_displayed_airports = settings.max_displayed_airports;
        persisted.active_window_min = settings.active_window_min;
        persisted.show_raw_metar = settings.show_raw_metar;
        persisted.debug_log_runway_matches = settings.debug_log_runway_matches;
        persisted.pressure_unit = (settings.pressure_unit == core::PressureUnit::kHpa) ? 1 : 0;
        persisted.auto_open_on_startup = settings.auto_open_on_startup;
        switch (settings.advisory_display_mode) {
            case core::AdvisoryDisplayMode::kNaturalLanguage:
                persisted.advisory_display_mode = 1;
                break;
            case core::AdvisoryDisplayMode::kBoth:
                persisted.advisory_display_mode = 2;
                break;
            default:
                persisted.advisory_display_mode = 0;
                break;
        }
        sdk::SaveSettings(persisted);
    }

    g_mainWindow.reset();
    g_tcasSource.reset();
    g_ltapiSource.reset();

    if (g_pluginMenu) {
        XPLMDestroyMenu(g_pluginMenu);
        g_pluginMenu = nullptr;
    }
    if (g_pluginMenuContainerItem >= 0) {
        XPLMRemoveMenuItem(XPLMFindPluginsMenu(), g_pluginMenuContainerItem);
        g_pluginMenuContainerItem = -1;
    }
    g_menuShowItemIndex = -1;
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int, void*) {}
