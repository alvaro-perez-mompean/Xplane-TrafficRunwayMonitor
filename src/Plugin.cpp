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

#include <algorithm>
#include <cstdint>
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
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMUtilities.h"

#include "core/AdvisoryFormat.h"
#include "core/Aggregator.h"
#include "core/AptDat.h"
#include "core/Cifp.h"
#include "core/EventLog.h"
#include "core/FmsOrigin.h"
#include "core/PhaseClassifier.h"
#include "core/RunwayMatcher.h"
#include "core/SightingTracker.h"
#include "core/TrendFilter.h"
#include "core/WindEstimate.h"

#include "sdk/AptDatLoader.h"
#include "sdk/CifpLoader.h"
#include "sdk/FmsOrigin.h"
#include "sdk/Log.h"
#include "sdk/LtapiSource.h"
#include "sdk/SettingsStore.h"
#include "sdk/SimbriefClient.h"
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

// How long the Flight Plan tab's "Loaded XXXX -> YYYY" success message stays
// visible after a fetch completes -- see g_simbriefSuccessMessageShownAtSec.
constexpr double kSimbriefSuccessMessageTtlSec = 3.0;

std::unique_ptr<sdk::TcasSource> g_tcasSource;
std::unique_ptr<sdk::LtapiSource> g_ltapiSource;
sdk::FmsOrigin g_fmsOrigin;
sdk::Weather g_weather;
sdk::SimbriefClient g_simbriefClient;

// Generation last applied from g_simbriefClient.Poll() into
// g_originOverride/g_destinationOverride below -- see
// sdk::SimbriefFetchResult::generation's own comment.
std::uint64_t g_appliedSimbriefGeneration = 0;

// nowSec of the most recent successful fetch's message -- SimbriefClient::
// Poll() keeps returning kSuccess indefinitely until the next fetch, so
// without this the "Loaded..." message would otherwise linger forever
// instead of clearing after kSimbriefSuccessMessageTtlSec.
double g_simbriefSuccessMessageShownAtSec = -kSimbriefSuccessMessageTtlSec;

// Generation up through which a finished Simbrief result (kSuccess/kError)
// is treated as belonging to a previous flight and displayed as kIdle
// instead -- see RunAnalysisCycle's simbriefResultIsStale. Set to the
// current generation by ResetFlightPlanForNewFlight(); otherwise
// SimbriefClient's cached result would keep resurrecting the previous
// flight's toast/route every cycle indefinitely, since Poll() only changes
// what it returns on the *next* explicit fetch.
std::uint64_t g_simbriefDismissedGeneration = 0;

// The pinned origin/destination (see
// notes/features/manual-origin-destination-override.md), session-only,
// never persisted via sdk::SettingsStore. Continuously re-seeded from the
// live source value every cycle its field is fresh, so it's always ready
// to carry over -- unchanged -- the instant staleness begins, rather than
// dropping to blank; the user can then type over it. Cleared only by an
// actual new-flight signal (XPluginReceiveMessage below), never by
// staleness alone.
std::optional<std::string> g_originOverride;
std::optional<std::string> g_destinationOverride;

// Bumped whenever the pin above is cleared for a new flight -- mirrored
// into ui::DisplayState::flight_reset_epoch so
// ui::RenderIcaoOverrideField can force its InputText buffer to follow
// even when a field's editable state doesn't itself change across the
// reset (e.g. stale on both the old and new flight).
int g_flightResetEpoch = 0;

// Bumped whenever g_originOverride/g_destinationOverride is set by
// something other than the user typing into the Flight Plan tab's own
// field -- currently only a successful Simbrief fetch. Mirrored into
// ui::DisplayState::origin_override_epoch/destination_override_epoch --
// see that field's own comment for why this is needed alongside
// g_flightResetEpoch.
int g_originOverrideEpoch = 0;
int g_destinationOverrideEpoch = 0;

// Flight Plan tab, Procedures section: last-known raw route/planned-runway
// figures from a Simbrief fetch -- same not-time-limited lifetime as
// ui::DisplayState::simbrief_route_text (reset on kIdle/new flight, left
// untouched on kFetching/kError, replaced on kSuccess). Kept as plain
// globals rather than in ui::DisplayState since ui:: never renders these
// directly -- they only feed core::ExtractDepartureAnchorFix/
// ExtractArrivalAnchorFix and the runway-selector defaults below.
std::optional<std::string> g_simbriefRawRoute;
std::optional<std::string> g_simbriefOriginPlannedRunway;
std::optional<std::string> g_simbriefDestinationPlannedRunway;

// User-overridable procedure selections (Flight Plan tab's Procedures
// section) -- "auto but overridable", same spirit as g_originOverride/
// g_destinationOverride: nullopt means "not yet picked", in which case
// ResolveProcedureSelections seeds a default every cycle; once set, a
// selection is left alone even if the candidate list it came from changes,
// until ResetFlightPlanForNewFlight or an origin/destination ICAO change
// invalidates it (see g_departureSelectionsIcao/g_arrivalSelectionsIcao).
std::optional<std::string> g_selectedDepartureRunway;
std::optional<std::string> g_selectedArrivalRunway;
std::optional<std::string> g_selectedSid;
std::optional<std::string> g_selectedStar;
std::optional<std::string> g_selectedApproach;

// The ICAO g_selectedDepartureRunway/g_selectedSid (resp.
// g_selectedArrivalRunway/g_selectedStar/g_selectedApproach) were last
// resolved for -- an origin/destination ICAO change (a manual retype, not
// necessarily a full new-flight reset) invalidates the old picks, since a
// runway/SID id from one airport is generally meaningless at another.
std::string g_departureSelectionsIcao;
std::string g_arrivalSelectionsIcao;

// CIFP data for whichever ICAO it was last loaded for -- reloaded only on
// an actual ICAO change (see EnsureCifpLoaded), not every cycle, since it's
// real filesystem I/O. nullopt covers both "not loaded yet" and "loaded,
// but this airport has no CIFP coverage at all" -- see
// sdk::LoadCifpForAirport's own comment.
std::optional<core::CifpProcedures> g_originCifp;
std::string g_originCifpIcao;
std::optional<core::CifpProcedures> g_destinationCifp;
std::string g_destinationCifpIcao;

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

// Every runway id an airport has (apt.dat already lists each physical end
// separately, e.g. "09" and "27" as distinct entries), sorted for a stable
// combo-box order. Empty if `airport` is null (unknown ICAO).
std::vector<std::string> RunwayCandidates(const core::Airport* airport)
{
    std::vector<std::string> ids;
    if (airport == nullptr) {
        return ids;
    }
    for (const core::RunwayEnd& rwy : airport->runways) {
        ids.push_back(rwy.id);
    }
    std::sort(ids.begin(), ids.end());
    return ids;
}

// Loads `cache` for `icao` if it isn't already loaded for that exact ICAO --
// real filesystem I/O (sdk::LoadCifpForAirport), so this only actually runs
// on an ICAO change, not every ~1Hz cycle. A nullopt `icao` clears the
// cache (no origin/destination resolved this cycle).
void EnsureCifpLoaded(const std::optional<std::string>& icao, std::optional<core::CifpProcedures>& cache,
                       std::string& cachedIcao)
{
    if (!icao.has_value()) {
        cache.reset();
        cachedIcao.clear();
        return;
    }
    if (*icao == cachedIcao) {
        return;
    }
    cachedIcao = *icao;
    cache = sdk::LoadCifpForAirport(*icao);
}

// Flight Plan tab's Procedures section: recomputes runway/SID/STAR/approach
// candidates and the display summary every cycle from the current origin/
// destination + Simbrief route, applying a default selection only when the
// user hasn't already picked one for this airport (see
// g_selectedDepartureRunway's own comment on the "auto but overridable"
// design).
void ResolveProcedureSelections(const sdk::FmsOriginDestination& fms, double nowSec)
{
    ui::DisplayState& display = g_mainWindow->display;

    const std::string originIcao = fms.origin_icao.value_or(std::string());
    const std::string destinationIcao = fms.destination_icao.value_or(std::string());

    if (originIcao != g_departureSelectionsIcao) {
        g_departureSelectionsIcao = originIcao;
        g_selectedDepartureRunway.reset();
        g_selectedSid.reset();
    }
    if (destinationIcao != g_arrivalSelectionsIcao) {
        g_arrivalSelectionsIcao = destinationIcao;
        g_selectedArrivalRunway.reset();
        g_selectedStar.reset();
        g_selectedApproach.reset();
    }

    EnsureCifpLoaded(fms.origin_icao, g_originCifp, g_originCifpIcao);
    EnsureCifpLoaded(fms.destination_icao, g_destinationCifp, g_destinationCifpIcao);

    const core::Airport* originAirport = fms.origin_icao ? FindAirport(g_airportDatabase, *fms.origin_icao) : nullptr;
    const core::Airport* destinationAirport =
        fms.destination_icao ? FindAirport(g_airportDatabase, *fms.destination_icao) : nullptr;

    display.departure_runway_candidates = RunwayCandidates(originAirport);
    display.arrival_runway_candidates = RunwayCandidates(destinationAirport);

    // Which runway(s) are actually active right now (confirmed-traffic tier
    // only, core::CategoryResult::active) -- so a Simbrief-planned runway
    // that's no longer in use stands out in the selectors above rather than
    // silently being wrong. history/wind_estimate are irrelevant here and
    // discarded.
    const double historyWindowSec = g_aggregatorConfig.active_window_sec * g_aggregatorConfig.history_window_multiplier;
    display.active_departure_runways.clear();
    if (fms.origin_icao.has_value() && originAirport != nullptr) {
        const core::CategoryResult departures =
            core::BuildCategoryResult(g_sightingTracker.FindSightings(*fms.origin_icao, core::SightingCategory::kDeparture),
                                       g_aggregatorConfig.active_window_sec, historyWindowSec, nowSec, originAirport);
        for (const core::RunwaySightingSummary& s : departures.active) {
            display.active_departure_runways.push_back(s.runway_id);
        }
    }
    display.active_arrival_runways.clear();
    if (fms.destination_icao.has_value() && destinationAirport != nullptr) {
        const core::CategoryResult arrivals = core::BuildCategoryResult(
            g_sightingTracker.FindSightings(*fms.destination_icao, core::SightingCategory::kArrival),
            g_aggregatorConfig.active_window_sec, historyWindowSec, nowSec, destinationAirport);
        for (const core::RunwaySightingSummary& s : arrivals.active) {
            display.active_arrival_runways.push_back(s.runway_id);
        }
    }

    // Departure runway: default to Simbrief's own plan_rwy if it's a real
    // runway at this airport, else just the first candidate -- something
    // has to be selected for SID matching to run at all, and either default
    // is freely overridable afterward.
    if (!g_selectedDepartureRunway.has_value()) {
        if (g_simbriefOriginPlannedRunway.has_value() &&
            std::find(display.departure_runway_candidates.begin(), display.departure_runway_candidates.end(),
                       *g_simbriefOriginPlannedRunway) != display.departure_runway_candidates.end()) {
            g_selectedDepartureRunway = g_simbriefOriginPlannedRunway;
        } else if (!display.departure_runway_candidates.empty()) {
            g_selectedDepartureRunway = display.departure_runway_candidates.front();
        }
    }
    if (!g_selectedArrivalRunway.has_value()) {
        if (g_simbriefDestinationPlannedRunway.has_value() &&
            std::find(display.arrival_runway_candidates.begin(), display.arrival_runway_candidates.end(),
                       *g_simbriefDestinationPlannedRunway) != display.arrival_runway_candidates.end()) {
            g_selectedArrivalRunway = g_simbriefDestinationPlannedRunway;
        } else if (!display.arrival_runway_candidates.empty()) {
            g_selectedArrivalRunway = display.arrival_runway_candidates.front();
        }
    }
    display.selected_departure_runway = g_selectedDepartureRunway;
    display.selected_arrival_runway = g_selectedArrivalRunway;

    // SID: anchor fix comes from Simbrief's raw route (nullopt if no fetch
    // yet) -- no route, no CIFP data, or no selected runway all mean no
    // candidates rather than a guess.
    display.sid_candidates.clear();
    display.sid_anchor_fix.reset();
    if (g_simbriefRawRoute.has_value() && g_originCifp.has_value() && g_selectedDepartureRunway.has_value()) {
        display.sid_anchor_fix = core::ExtractDepartureAnchorFix(*g_simbriefRawRoute, g_originCifp->sids,
                                                                   *g_selectedDepartureRunway);
        if (display.sid_anchor_fix.has_value()) {
            display.sid_candidates =
                core::FindSidsForRunwayFix(g_originCifp->sids, *g_selectedDepartureRunway, *display.sid_anchor_fix);
        }
    }
    if (!g_selectedSid.has_value() && !display.sid_candidates.empty()) {
        g_selectedSid = display.sid_candidates.front();
    }
    display.selected_sid = g_selectedSid;

    // STAR: runway-independent (see core::FindStarsForFix's own comment).
    display.star_candidates.clear();
    display.star_anchor_fix.reset();
    if (g_simbriefRawRoute.has_value() && g_destinationCifp.has_value()) {
        display.star_anchor_fix = core::ExtractArrivalAnchorFix(*g_simbriefRawRoute, g_destinationCifp->stars);
        if (display.star_anchor_fix.has_value()) {
            display.star_candidates = core::FindStarsForFix(g_destinationCifp->stars, *display.star_anchor_fix);
        }
    }
    if (!g_selectedStar.has_value() && !display.star_candidates.empty()) {
        g_selectedStar = display.star_candidates.front();
    }
    display.selected_star = g_selectedStar;

    // Approach: ident encodes the runway directly, no anchor fix needed.
    // Reformatted into the same shape a real FMS/MCDU shows (see
    // core::FormatApproachIdentForDisplay's own comment) -- the raw CIFP
    // ident is only ever a matching key, never shown to the user, so
    // g_selectedApproach holds the formatted string throughout rather than
    // needing a separate raw/display pair.
    display.approach_candidates.clear();
    if (g_destinationCifp.has_value() && g_selectedArrivalRunway.has_value()) {
        for (const std::string& ident : core::FindApproachesForRunway(g_destinationCifp->approaches,
                                                                        *g_selectedArrivalRunway)) {
            display.approach_candidates.push_back(core::FormatApproachIdentForDisplay(ident));
        }
    }
    if (!g_selectedApproach.has_value() && !display.approach_candidates.empty()) {
        g_selectedApproach = display.approach_candidates.front();
    }
    display.selected_approach = g_selectedApproach;

    display.procedure_summary_text.reset();
    if (fms.origin_icao.has_value() && fms.destination_icao.has_value()) {
        display.procedure_summary_text = core::FormatProcedureSummary(
            *fms.origin_icao, g_selectedDepartureRunway, g_selectedSid, g_selectedStar, *fms.destination_icao,
            g_selectedArrivalRunway, g_selectedApproach);
    }
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

    sdk::FmsOriginDestination fms = g_fmsOrigin.Resolve();

    // See g_originOverride's own comment for the sticky-pin/reset design.
    // Origin and destination unlock independently since the native FMS
    // path can have one entry present without the other.
    g_mainWindow->display.origin_editable = !fms.origin_fresh;
    g_mainWindow->display.destination_editable = !fms.destination_fresh;
    if (fms.origin_fresh) {
        g_originOverride = fms.origin_icao;
    }
    if (fms.destination_fresh) {
        g_destinationOverride = fms.destination_icao;
    }
    fms.origin_icao = core::ResolveEffectiveIcao(fms.origin_fresh, fms.origin_icao, g_originOverride);
    fms.destination_icao = core::ResolveEffectiveIcao(fms.destination_fresh, fms.destination_icao, g_destinationOverride);
    g_mainWindow->display.origin_icao = fms.origin_icao;
    g_mainWindow->display.destination_icao = fms.destination_icao;
    g_mainWindow->display.flight_reset_epoch = g_flightResetEpoch;
    g_mainWindow->display.origin_override_epoch = g_originOverrideEpoch;
    g_mainWindow->display.destination_override_epoch = g_destinationOverrideEpoch;

    // Flight Plan tab's "Fetch from Simbrief" button -- see
    // sdk::SimbriefClient's own comment. Poll() is cheap (mutex + copy),
    // safe every cycle. A freshly-completed success is applied into the
    // override exactly once via the generation counter, then flows through
    // the same core::ResolveEffectiveIcao precedence as a manual edit would
    // (native-FMS-fresh still wins outright) -- picked up on the *next*
    // cycle, same ~1s lag this override design already has elsewhere.
    const sdk::SimbriefFetchResult simbrief = g_simbriefClient.Poll();
    const bool simbriefGenerationChanged = simbrief.generation != g_appliedSimbriefGeneration;
    if (simbriefGenerationChanged && simbrief.status == sdk::SimbriefFetchStatus::kSuccess) {
        g_simbriefSuccessMessageShownAtSec = nowSec;
    }
    // A finished fetch (kSuccess/kError) whose generation predates the last
    // flight reset belongs to the previous flight -- fall through to kIdle
    // instead of resurrecting a stale toast/route every cycle. kFetching
    // has no generation of its own yet, so it's always current regardless.
    const bool simbriefResultIsStale =
        (simbrief.status == sdk::SimbriefFetchStatus::kSuccess || simbrief.status == sdk::SimbriefFetchStatus::kError) &&
        simbrief.generation <= g_simbriefDismissedGeneration;
    const sdk::SimbriefFetchStatus effectiveSimbriefStatus =
        simbriefResultIsStale ? sdk::SimbriefFetchStatus::kIdle : simbrief.status;
    switch (effectiveSimbriefStatus) {
        case sdk::SimbriefFetchStatus::kIdle:
            g_mainWindow->display.simbrief_fetch_status = ui::SimbriefFetchUiStatus::kIdle;
            g_mainWindow->display.simbrief_fetch_message.clear();
            g_mainWindow->display.simbrief_route_text.reset();
            g_mainWindow->display.simbrief_fuel = core::SimbriefFuelPlan{};
            g_mainWindow->display.simbrief_weights = core::SimbriefWeights{};
            g_mainWindow->display.simbrief_header = core::SimbriefHeader{};
            g_simbriefRawRoute.reset();
            g_simbriefOriginPlannedRunway.reset();
            g_simbriefDestinationPlannedRunway.reset();
            break;
        case sdk::SimbriefFetchStatus::kFetching:
            g_mainWindow->display.simbrief_fetch_status = ui::SimbriefFetchUiStatus::kFetching;
            g_mainWindow->display.simbrief_fetch_message = "Fetching...";
            // simbrief_route_text/simbrief_fuel/simbrief_weights/
            // simbrief_header/g_simbriefRawRoute/g_simbriefOrigin(Destination)
            // PlannedRunway deliberately left untouched -- keep showing the
            // last successful fetch while a re-fetch is in flight rather
            // than flashing it away.
            break;
        case sdk::SimbriefFetchStatus::kSuccess:
            g_mainWindow->display.simbrief_fetch_status = ui::SimbriefFetchUiStatus::kSuccess;
            if (nowSec - g_simbriefSuccessMessageShownAtSec < kSimbriefSuccessMessageTtlSec) {
                g_mainWindow->display.simbrief_fetch_message = "Loaded " + simbrief.origin_icao.value_or("----") +
                    " " + ui::kIconArrowRight + " " + simbrief.destination_icao.value_or("----");
            } else {
                g_mainWindow->display.simbrief_fetch_message.clear();
            }
            g_mainWindow->display.simbrief_route_text = simbrief.route_text;
            g_mainWindow->display.simbrief_fuel = simbrief.fuel;
            g_mainWindow->display.simbrief_weights = simbrief.weights;
            g_mainWindow->display.simbrief_header = simbrief.header;
            g_simbriefRawRoute = simbrief.raw_route;
            g_simbriefOriginPlannedRunway = simbrief.origin_planned_runway;
            g_simbriefDestinationPlannedRunway = simbrief.destination_planned_runway;
            break;
        case sdk::SimbriefFetchStatus::kError:
            g_mainWindow->display.simbrief_fetch_status = ui::SimbriefFetchUiStatus::kError;
            g_mainWindow->display.simbrief_fetch_message = simbrief.error_message;
            // simbrief_route_text/simbrief_fuel/simbrief_weights/
            // simbrief_header left untouched -- a failed re-fetch (e.g.
            // transient network blip) shouldn't wipe out the last
            // known-good flight plan.
            break;
    }
    if (simbriefGenerationChanged) {
        g_appliedSimbriefGeneration = simbrief.generation;
        if (simbrief.status == sdk::SimbriefFetchStatus::kSuccess) {
            // Same plumbing manual typing already uses -- just fills in the
            // override. fms.origin_fresh/destination_fresh (native FMS)
            // still wins every cycle above regardless. Bumping the
            // override epoch alongside is what makes an editable (no fresh
            // native-FMS entry) field's InputText buffer actually pick this
            // up -- see ui::DisplayState::origin_override_epoch.
            if (simbrief.origin_icao.has_value()) {
                g_originOverride = simbrief.origin_icao;
                ++g_originOverrideEpoch;
            }
            if (simbrief.destination_icao.has_value()) {
                g_destinationOverride = simbrief.destination_icao;
                ++g_destinationOverrideEpoch;
            }
        }
    }

    // Flight Plan tab validation feedback -- the airport name if the
    // pinned ICAO resolves in g_airportDatabase, nullopt (rendered as an
    // "unknown ICAO" warning by RenderIcaoOverrideField) otherwise.
    g_mainWindow->display.origin_airport_name.reset();
    if (fms.origin_icao.has_value()) {
        if (const core::Airport* originAirport = FindAirport(g_airportDatabase, *fms.origin_icao)) {
            g_mainWindow->display.origin_airport_name = originAirport->name;
        }
    }
    g_mainWindow->display.destination_airport_name.reset();
    if (fms.destination_icao.has_value()) {
        if (const core::Airport* destinationAirport = FindAirport(g_airportDatabase, *fms.destination_icao)) {
            g_mainWindow->display.destination_airport_name = destinationAirport->name;
        }
    }

    ResolveProcedureSelections(fms, nowSec);

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
    g_mainWindow->interaction.on_origin_override_changed = [](const std::string& icao) { g_originOverride = icao; };
    g_mainWindow->interaction.on_destination_override_changed = [](const std::string& icao) {
        g_destinationOverride = icao;
    };
    g_mainWindow->interaction.on_simbrief_fetch_requested = []() {
        g_simbriefClient.RequestFetch(g_mainWindow->settings.simbrief_pilot_id);
    };
    g_mainWindow->interaction.on_departure_runway_changed = [](const std::string& runway) {
        g_selectedDepartureRunway = runway;
        g_selectedSid.reset(); // the previous runway's SID pick is meaningless from a different runway
    };
    g_mainWindow->interaction.on_arrival_runway_changed = [](const std::string& runway) {
        g_selectedArrivalRunway = runway;
        g_selectedApproach.reset(); // approach idents are runway-specific -- see FindApproachesForRunway
    };
    g_mainWindow->interaction.on_sid_changed = [](const std::string& sid) { g_selectedSid = sid; };
    g_mainWindow->interaction.on_star_changed = [](const std::string& star) { g_selectedStar = star; };
    g_mainWindow->interaction.on_approach_changed = [](const std::string& approach) { g_selectedApproach = approach; };

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
        settings.simbrief_pilot_id = persisted->simbrief_pilot_id;
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
        persisted.simbrief_pilot_id = settings.simbrief_pilot_id;
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

// See g_originOverride/g_flightResetEpoch's own comments. Also dismisses
// the Simbrief fetch display state (status/message/route/fuel/weights/
// header) -- without this, a previous flight's "Loaded KJFK -> KLAX" toast
// or (worse, since they're not time-limited) its route line and
// fuel/weights/header blocks would otherwise linger into the new flight,
// since SimbriefClient::Poll() keeps returning the same cached result until
// the user explicitly fetches again. g_simbriefDismissedGeneration is what
// actually suppresses it (see RunAnalysisCycle's simbriefResultIsStale) --
// the immediate display clear here just avoids a one-cycle flash of the
// stale content before the next RunAnalysisCycle tick applies that.
//
// Called from XPluginReceiveMessage, which the SDK can invoke before this
// plugin's first XPluginEnable() and after XPluginDisable() (broadcast
// messages aren't gated on enabled state) -- g_mainWindow is only live
// between those two calls, so every g_mainWindow access here must stay
// behind this guard, same as RunAnalysisCycle's own.
void ResetFlightPlanForNewFlight()
{
    g_originOverride.reset();
    g_destinationOverride.reset();
    ++g_flightResetEpoch;

    // Procedures section (Flight Plan tab) is scoped to the flight it was
    // resolved for, same as the origin/destination pin above -- without
    // this, a repeat departure from the same ICAO would carry over the
    // previous flight's runway/SID/STAR/approach picks even though
    // RunAnalysisCycle's own ICAO-change check wouldn't catch it (the ICAO
    // hasn't changed, only the flight has). g_departureSelectionsIcao/
    // g_arrivalSelectionsIcao are deliberately left alone -- clearing them
    // too would just make ResolveProcedureSelections redo the same
    // ICAO-unchanged check and reach the same reset outcome next cycle.
    g_selectedDepartureRunway.reset();
    g_selectedArrivalRunway.reset();
    g_selectedSid.reset();
    g_selectedStar.reset();
    g_selectedApproach.reset();
    g_simbriefRawRoute.reset();
    g_simbriefOriginPlannedRunway.reset();
    g_simbriefDestinationPlannedRunway.reset();
    // Poll().generation only bumps when a fetch *completes* (kSuccess/
    // kError) -- kFetching leaves it unchanged. So a fetch still in flight
    // right now hasn't been counted yet; dismissing only through its
    // pre-completion generation would let that one fetch's result (still
    // belonging to the flight that's ending) slip through as "not stale"
    // once it finishes and bumps past this dismissal point. RequestFetch
    // never allows more than one fetch in flight at a time, so a flat +1
    // fully covers it.
    const sdk::SimbriefFetchResult pendingSimbrief = g_simbriefClient.Poll();
    g_simbriefDismissedGeneration =
        pendingSimbrief.generation + (pendingSimbrief.status == sdk::SimbriefFetchStatus::kFetching ? 1 : 0);
    if (g_mainWindow) {
        g_mainWindow->display.simbrief_fetch_status = ui::SimbriefFetchUiStatus::kIdle;
        g_mainWindow->display.simbrief_fetch_message.clear();
        g_mainWindow->display.simbrief_route_text.reset();
        g_mainWindow->display.simbrief_fuel = core::SimbriefFuelPlan{};
        g_mainWindow->display.simbrief_weights = core::SimbriefWeights{};
        g_mainWindow->display.simbrief_header = core::SimbriefHeader{};
    }
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID, int inMessage, void* inParam)
{
    // The pinned origin/destination is scoped to the flight it was
    // resolved/typed for (see notes/features/manual-origin-destination-override.md)
    // -- staleness alone deliberately does NOT clear it (see
    // g_originOverride's own comment), so without this handler a value
    // from the previous flight would keep getting spliced into
    // fms.origin_icao/destination_icao indefinitely once the new flight's
    // FMS route also stays empty (never becomes fresh).
    //
    // Two triggers, deliberately both: XPLM_MSG_PLANE_LOADED (aircraft
    // model itself changes -- the original reported bug's case, param is
    // the plane index, 0 = the user's own plane) and
    // XPLM_MSG_AIRPORT_LOADED (the user's plane is repositioned at a new
    // airport -- fires on Start Flight/situation load/replay start even
    // when the aircraft type is unchanged, e.g. restarting the same
    // aircraft for a new route). PLANE_LOADED alone would miss that second case.
    // Resetting on either is harmless even when redundant (e.g. both firing
    // for the same restart) -- this is session-only UI state, not anything
    // that needs debouncing.
    if (inMessage == XPLM_MSG_AIRPORT_LOADED) {
        ResetFlightPlanForNewFlight();
        return;
    }
    // XPLM_MSG_PLANE_LOADED's param is the plane index bit-cast to a
    // pointer, not a real pointer -- reinterpret_cast<intptr_t> reads it
    // back as the integer X-Plane sent, per XPLMPlugin.h's own comment on
    // this message.
    if (inMessage == XPLM_MSG_PLANE_LOADED &&
        reinterpret_cast<intptr_t>(inParam) == XPLM_USER_AIRCRAFT) {
        ResetFlightPlanForNewFlight();
    }
}
