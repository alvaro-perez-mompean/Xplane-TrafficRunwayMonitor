#pragma once

#include <optional>
#include <string>

#include "core/AptDat.h"

// Wind-favored-runway-end estimate: fallback runway guess when no
// traffic-confirmed runway exists yet (Aggregator).
//
// This module does no XPLM/SDK calls itself: both wind readings below must
// already be resolved by the caller (sdk/Weather, wrapping
// XPLMGetWeatherAtLocation and the sim/weather/region/ array fallback) --
// the *choice* of which reading to trust and why is what lives here,
// not the reading itself.

namespace trm::core {

// Source tiers, most to least trustworthy. Only kStation/kRegional/
// kAircraftPosition are ever produced by EstimateWindFavoredRunwayEnd
// itself; "own_station" is an upgrade that happens one level up, in
// Aggregator::BuildAirportEntry, once it separately confirms this exact
// airport also has its own METAR on file (see UpgradeToOwnStationIfConfirmed
// below, which it calls).
enum class WindEstimateSource { kOwnStation, kStation, kRegional, kAircraftPosition };

// One already-resolved wind reading. `has_station_match` is only
// meaningful for an airport-position reading (sdk/Weather's own
// XPLMGetWeatherAtLocation wrapper) -- it's that SDK call's own return
// value: true if it matched real station-specific METAR data, false if it
// fell back to the simulator's regional/interpolated model for that point.
struct WindReading {
    double speed_kt = 0.0;
    double direction_true_deg = 0.0; // wind FROM direction, true
    bool has_station_match = false;
    // Station pressure (QNH-equivalent) from the same underlying
    // XPLMWeatherInfo_t sample, in Pascals. Piggybacks on WindReading
    // instead of a separate query so sdk::Weather doesn't have to hit
    // XPLMGetWeatherAtLocation twice for the same position. 0.0 for the
    // region-array wind fallback, which carries no pressure data.
    double pressure_pa = 0.0;
};

struct WindEstimateConfig {
    double min_speed_kt = 1.0; // a dead calm favors nothing
};

struct WindEstimateResult {
    std::string runway_id;
    WindEstimateSource source = WindEstimateSource::kRegional;
};

// The wind itself, independent of any runway -- unlike WindEstimateResult
// (which only exists as a fallback when no traffic-confirmed runway data
// exists yet), this is meant to be displayed unconditionally, any time a
// reading is available at all.
struct WindInfo {
    double speed_kt = 0.0;
    double direction_true_deg = 0.0; // wind FROM direction, true
    WindEstimateSource source = WindEstimateSource::kRegional;
    bool is_calm = false; // speed_kt below config.min_speed_kt -- direction is noise, not meaningful
};

// Picks between the two already-resolved readings -- airport-position wins
// whenever present (even dead calm, trusted over the fallback), else
// aircraft-position -- and tags the result with the source it came from.
// This is the reading-selection half of EstimateWindFavoredRunwayEnd below,
// factored out so callers that only want "what's the current wind" don't
// need an Airport (with runway headings) to get an answer. nullopt only
// when neither reading is available at all.
std::optional<WindInfo> ResolveEffectiveWind(const std::optional<WindReading>& airportPositionReading,
                                              const std::optional<WindReading>& aircraftPositionReading,
                                              const WindEstimateConfig& config = {});

// `airportPositionReading` is wind queried at the airport's own
// position/elevation; nullopt if that API was unavailable, the airport has
// no reference point to query at, or the query itself returned nothing
// usable this cycle. `aircraftPositionReading` is the region-array
// fallback, only ever consulted if `airportPositionReading` is nullopt.
// Note the (perhaps non-obvious) consequence that a real but *dead-calm*
// airport-position reading is trusted as final and does NOT fall through
// to the aircraft-position reading, even if the latter would show real wind.
std::optional<WindEstimateResult> EstimateWindFavoredRunwayEnd(const Airport& airport,
                                                                 const std::optional<WindReading>& airportPositionReading,
                                                                 const std::optional<WindReading>& aircraftPositionReading,
                                                                 const WindEstimateConfig& config = {});

// A "station" match doesn't disclose which station -- could be this
// airport's own, or a neighbor whose coverage radius reaches here. If this
// exact airport also has its own METAR on file, that confirms it.
WindEstimateSource UpgradeToOwnStationIfConfirmed(WindEstimateSource source, bool metarAvailableForThisAirport);

// Human-readable label for a source tier, for UI tooltips.
std::string WindEstimateSourceLabel(WindEstimateSource source);

} // namespace trm::core
