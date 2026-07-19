#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/AptDat.h"
#include "core/SightingTracker.h"
#include "core/WindEstimate.h"

// Active/History/Wind-estimate aggregation and pinned-entry logic.
//
// Weather/METAR readings are XPLM SDK calls (sdk/Weather, a later module)
// and must already be resolved by the caller -- this module does no
// XPLM/SDK calls itself.

namespace trm::core {

struct RunwaySightingSummary {
    std::string runway_id;
    int count = 0;                    // distinct aircraft within the query window
    double elapsed_sec = 0.0;         // time since the most recent of those
    std::optional<double> length_ft;  // nil if the runway isn't in the apt.dat database

    // True when this entry was never itself observed in this category, but
    // added because the airport has only one physical runway (see
    // core::Airport::IsSingleRunwayAirport) and the *other* category
    // (arrival<->departure) is active on this runway_id -- there's no other
    // pavement the other category could be using. count is always 0 here:
    // it deliberately does NOT count as a distinct-aircraft sighting of its
    // own, only as "this is the runway in use". See BuildAirportEntry.
    bool inferred = false;
};

// Unsorted -- callers sort by
// whichever field matters for their use (BuildCategoryResult below sorts
// by count descending for "active", elapsed_sec ascending for "history").
// `airport` (nullable) is only used for the length_ft lookup; nullopt if
// not supplied.
std::vector<RunwaySightingSummary> RunwaysWithSightings(const RunwaySightings* categorySightings, double windowSec,
                                                          double nowSec, const Airport* airport);

// One category's (arrival or departure) resolved display state.
struct CategoryResult {
    std::vector<RunwaySightingSummary> active;   // sorted by count descending
    std::optional<RunwaySightingSummary> history; // only set when active is empty

    bool NeedsWindEstimate() const { return active.empty() && !history.has_value(); }
};

// Active/history cascade: runways active within activeWindowSec (ranked by distinct-aircraft
// count), or if none, a single history pick from the longer
// historyWindowSec lookback (picked by most-recent, not by count).
CategoryResult BuildCategoryResult(const RunwaySightings* categorySightings, double activeWindowSec,
                                    double historyWindowSec, double nowSec, const Airport* airport);

struct AirportEntry {
    std::string icao;
    std::optional<std::string> name; // nullopt if airport unknown or apt.dat row had no name
    std::optional<double> distance_nm;
    CategoryResult arrivals;
    CategoryResult departures;
    // Shared fallback for whichever category(ies) needed it: exactly one
    // wind estimate is computed per airport (not one per category),
    // triggered if EITHER category has neither active nor history data.
    std::optional<WindEstimateResult> wind_estimate;
    // The current wind itself, unconditionally populated whenever a
    // reading is available -- unlike wind_estimate above, not gated behind
    // any category needing it. This is what the dashboard displays as
    // "current wind" independent of runway-favoring logic.
    std::optional<WindInfo> current_wind;
    // Altimeter setting (QNH-equivalent) in Pascals, from the airport-
    // position weather reading only -- unlike current_wind, never falls
    // back to the aircraft-position/region reading, which carries no
    // pressure data of its own. nullopt if no airport-position reading was
    // available this cycle.
    std::optional<double> altimeter_pa;
    std::optional<std::string> metar;
};

// Already-resolved external inputs this module cannot fetch itself
// (weather/METAR are XPLM SDK calls -- sdk/Weather, a later module).
struct AirportEntryInputs {
    std::optional<WindReading> wind_airport_position_reading;
    std::optional<WindReading> wind_aircraft_position_reading;
    std::optional<std::string> metar;
};

struct AggregatorConfig {
    double active_window_sec = 1800.0; // 30 min, user-adjustable
    int history_window_multiplier = 3;
    double wind_estimate_min_speed_kt = 1.0;
    double origin_pin_radius_nm = 10.0;
};

// `airport` (nullable) is the apt.dat
// entry for `icao`, if known -- wind estimate and length_ft lookups are
// skipped without it.
AirportEntry BuildAirportEntry(const std::string& icao, std::optional<double> distanceNm, const Airport* airport,
                                const SightingTracker& sightingTracker, const AirportEntryInputs& inputs,
                                double nowSec, const AggregatorConfig& config = {});

struct NearbyCandidate {
    std::string icao;
    std::string name;
    double distance_nm = 0.0;
};

// Every nearby airport (already
// sorted nearest-first -- AptDat::FindNearestAirports) excluding the
// pinned one, capped at maxDisplayed. The pinned entry itself is exempt
// from both the radius and this cap (it isn't built from this list at
// all).
std::vector<NearbyCandidate> BuildNearbyCandidates(const std::vector<NearbyAirport>& nearestAirports,
                                                    const std::optional<std::string>& pinnedIcao, int maxDisplayed);

enum class PinnedKind { kOrigin, kDestination };

struct PinnedSelection {
    std::string icao;
    PinnedKind kind;
};

// Selection logic: with both known,
// origin is shown while within originPinRadiusNm of it, else destination;
// with only one known, that one is shown; with neither, nullopt.
// `originDistanceNm` is only consulted (and only needed) when both origin
// and destination are known.
std::optional<PinnedSelection> SelectPinnedAirport(const std::optional<std::string>& originIcao,
                                                    const std::optional<std::string>& destinationIcao,
                                                    std::optional<double> originDistanceNm,
                                                    double originPinRadiusNm = 10.0);

struct PinnedEntryResult {
    AirportEntry entry;
    PinnedKind kind;
};

// Selection (SelectPinnedAirport)
// plus the full display entry (BuildAirportEntry) for whichever airport was
// selected. Returns nullopt if neither origin nor destination is known.
std::optional<PinnedEntryResult> BuildPinnedEntry(const std::optional<std::string>& originIcao,
                                                   const std::optional<std::string>& destinationIcao,
                                                   const AirportDatabase& db, double userLatDeg, double userLonDeg,
                                                   const SightingTracker& sightingTracker,
                                                   const AirportEntryInputs& inputs, double nowSec,
                                                   const AggregatorConfig& config = {});

} // namespace trm::core
