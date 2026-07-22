#include "core/Aggregator.h"

#include <algorithm>

namespace trm::core {

namespace {

// Single-runway airports have exactly one physical strip shared by both
// categories -- if traffic is confirmed active on it in one category,
// there's no other pavement the other category could be using, even before
// that other category has any sightings of its own. `sourceActive` is a
// snapshot taken before either category is mutated, so cross-applying both
// directions in the same call doesn't feed one direction's inferred entries
// back into the other. Deliberately only touches `target.active`, never
// synthesizes a history pick: an inferred *active* entry answers "what
// runway is in use", which a fabricated history entry has no real-world
// analogue for.
void ApplySingleRunwayInference(CategoryResult& target, const std::vector<RunwaySightingSummary>& sourceActive)
{
    for (const RunwaySightingSummary& sourceRunway : sourceActive) {
        const bool alreadyPresent =
            std::any_of(target.active.begin(), target.active.end(),
                        [&](const RunwaySightingSummary& r) { return r.runway_id == sourceRunway.runway_id; });
        if (alreadyPresent) {
            continue;
        }
        RunwaySightingSummary inferred;
        inferred.runway_id = sourceRunway.runway_id;
        inferred.count = 0;
        inferred.elapsed_sec = sourceRunway.elapsed_sec;
        inferred.length_ft = sourceRunway.length_ft;
        inferred.inferred = true;
        target.active.push_back(std::move(inferred));
    }
    if (!target.active.empty()) {
        target.history.reset(); // keep CategoryResult's own "history only when active is empty" invariant
    }
}

// The fallback cascade for one category: X-Plane's own authored flow rules
// first, then the crosswind heuristic. Only reached when the category has
// neither active nor history sightings.
//
// Note the calm gate applies to the crosswind tier ONLY. A dead calm still has
// to consult flows: the leading clause of a multi-rule flow is almost always
// the calm-wind one, and answering "nothing is favored" there would throw away
// the most confident answer available (LEPA in a dead calm is 24L/24R, named
// and authored, not a coin flip).
std::optional<RunwayEstimate> ResolveCategoryEstimate(const Airport& airport, const AirportEntryInputs& inputs,
                                                       RunwayOperation operation, const WindEstimateConfig& windConfig)
{
    const std::optional<WindInfo> wind =
        ResolveEffectiveWind(inputs.wind_airport_position_reading, inputs.wind_aircraft_position_reading, windConfig);
    if (!wind) {
        return std::nullopt; // no reading at all: flow wind rules can't be evaluated either
    }

    ActiveRunwayConditions conditions;
    conditions.wind_from_true_deg = wind->direction_true_deg;
    conditions.wind_speed_kt = wind->speed_kt;
    conditions.utc_minutes = inputs.utc_minutes;
    // Only the airport-position reading carries ceiling/visibility, and it is
    // also the one ResolveEffectiveWind prefers whenever present, so there is
    // no case where the other reading's values would be the ones we want. Left
    // unrestricted otherwise, which cannot wrongly fail a flow.
    if (inputs.wind_airport_position_reading) {
        conditions.ceiling_ft = inputs.wind_airport_position_reading->ceiling_ft;
        conditions.visibility_sm = inputs.wind_airport_position_reading->visibility_sm;
    }
    // aircraft_class deliberately left empty: we are predicting which runway
    // the next unknown aircraft will use, not routing a specific one.

    // Hand the crosswind tier the same calm threshold this function gates on
    // below. Left at ActiveRunwayConfig's own (higher) default, a wind between
    // the two -- 1kt to 3kt with the defaults -- would pass the is_calm check
    // here while the crosswind tier had already given up on direction and
    // short-circuited to its lowest-id tie-break, so the UI would present an
    // arbitrary runway as "wind favors" it. EstimateWindFavoredRunwayEnd
    // already does exactly this for the same reason.
    ActiveRunwayConfig activeRunwayConfig;
    activeRunwayConfig.calm_wind_kt = windConfig.min_speed_kt;

    const std::optional<ActiveRunwayResult> selected =
        SelectActiveRunway(airport, conditions, operation, activeRunwayConfig);
    if (!selected) {
        return std::nullopt;
    }
    if (selected->source == ActiveRunwaySource::kCrosswind && wind->is_calm) {
        return std::nullopt;
    }

    RunwayEstimate estimate;
    estimate.runway_id = selected->runway_id;
    estimate.source = wind->source;
    estimate.rule_source = selected->source;
    estimate.flow_name = selected->flow_name;
    return estimate;
}

} // namespace

std::vector<RunwaySightingSummary> RunwaysWithSightings(const RunwaySightings* categorySightings, double windowSec,
                                                          double nowSec, const Airport* airport)
{
    std::vector<RunwaySightingSummary> results;
    if (!categorySightings) {
        return results;
    }

    for (const auto& [runwayId, contributors] : *categorySightings) {
        int count = 0;
        std::optional<double> mostRecent;
        for (const auto& [slotIndex, lastSeen] : contributors) {
            (void)slotIndex;
            if (nowSec - lastSeen <= windowSec) {
                ++count;
                if (!mostRecent || lastSeen > *mostRecent) {
                    mostRecent = lastSeen;
                }
            }
        }
        if (count > 0) {
            RunwaySightingSummary summary;
            summary.runway_id = runwayId;
            summary.count = count;
            summary.elapsed_sec = nowSec - *mostRecent;
            summary.length_ft = airport ? FindRunwayLengthFt(*airport, runwayId) : std::nullopt;
            results.push_back(std::move(summary));
        }
    }

    return results;
}

CategoryResult BuildCategoryResult(const RunwaySightings* categorySightings, double activeWindowSec,
                                    double historyWindowSec, double nowSec, const Airport* airport)
{
    CategoryResult result;
    result.active = RunwaysWithSightings(categorySightings, activeWindowSec, nowSec, airport);
    std::stable_sort(result.active.begin(), result.active.end(),
                      [](const RunwaySightingSummary& a, const RunwaySightingSummary& b) { return a.count > b.count; });

    if (result.active.empty()) {
        std::vector<RunwaySightingSummary> candidates =
            RunwaysWithSightings(categorySightings, historyWindowSec, nowSec, airport);
        std::stable_sort(candidates.begin(), candidates.end(),
                          [](const RunwaySightingSummary& a, const RunwaySightingSummary& b) {
                              return a.elapsed_sec < b.elapsed_sec;
                          });
        if (!candidates.empty()) {
            result.history = candidates.front();
        }
    }

    return result;
}

AirportEntry BuildAirportEntry(const std::string& icao, std::optional<double> distanceNm, const Airport* airport,
                                const SightingTracker& sightingTracker, const AirportEntryInputs& inputs,
                                double nowSec, const AggregatorConfig& config)
{
    const double activeWindowSec = config.active_window_sec;
    const double historyWindowSec = activeWindowSec * config.history_window_multiplier;

    AirportEntry entry;
    entry.icao = icao;
    if (airport && !airport->name.empty()) {
        entry.name = airport->name;
    }
    entry.distance_nm = distanceNm;
    entry.arrivals = BuildCategoryResult(sightingTracker.FindSightings(icao, SightingCategory::kArrival),
                                          activeWindowSec, historyWindowSec, nowSec, airport);
    entry.departures = BuildCategoryResult(sightingTracker.FindSightings(icao, SightingCategory::kDeparture),
                                            activeWindowSec, historyWindowSec, nowSec, airport);

    if (airport && airport->IsSingleRunwayAirport()) {
        const std::vector<RunwaySightingSummary> arrivalsActiveSnapshot = entry.arrivals.active;
        const std::vector<RunwaySightingSummary> departuresActiveSnapshot = entry.departures.active;
        ApplySingleRunwayInference(entry.arrivals, departuresActiveSnapshot);
        ApplySingleRunwayInference(entry.departures, arrivalsActiveSnapshot);
    }

    WindEstimateConfig windConfig;
    windConfig.min_speed_kt = config.wind_estimate_min_speed_kt;

    entry.current_wind =
        ResolveEffectiveWind(inputs.wind_airport_position_reading, inputs.wind_aircraft_position_reading, windConfig);

    if (inputs.wind_airport_position_reading.has_value()) {
        entry.altimeter_pa = inputs.wind_airport_position_reading->pressure_pa;
    }

    if (airport) {
        if (entry.arrivals.NeedsEstimate()) {
            entry.arrivals_estimate = ResolveCategoryEstimate(*airport, inputs, RunwayOperation::kArrival, windConfig);
        }
        if (entry.departures.NeedsEstimate()) {
            entry.departures_estimate =
                ResolveCategoryEstimate(*airport, inputs, RunwayOperation::kDeparture, windConfig);
        }
    }

    entry.metar = inputs.metar;

    for (std::optional<RunwayEstimate>* estimate : {&entry.arrivals_estimate, &entry.departures_estimate}) {
        if (*estimate) {
            (*estimate)->source = UpgradeToOwnStationIfConfirmed((*estimate)->source, entry.metar.has_value());
        }
    }
    if (entry.current_wind) {
        entry.current_wind->source =
            UpgradeToOwnStationIfConfirmed(entry.current_wind->source, entry.metar.has_value());
    }

    return entry;
}

std::vector<NearbyCandidate> BuildNearbyCandidates(const std::vector<NearbyAirport>& nearestAirports,
                                                    const std::optional<std::string>& pinnedIcao, int maxDisplayed)
{
    std::vector<NearbyCandidate> candidates;
    for (const NearbyAirport& airport : nearestAirports) {
        if (static_cast<int>(candidates.size()) >= maxDisplayed) {
            break;
        }
        if (!pinnedIcao || airport.icao != *pinnedIcao) {
            candidates.push_back(NearbyCandidate{airport.icao, airport.name, airport.distance_nm});
        }
    }
    return candidates;
}

std::optional<PinnedSelection> SelectPinnedAirport(const std::optional<std::string>& originIcao,
                                                    const std::optional<std::string>& destinationIcao,
                                                    std::optional<double> originDistanceNm,
                                                    double originPinRadiusNm)
{
    if (originIcao && destinationIcao) {
        if (originDistanceNm && *originDistanceNm <= originPinRadiusNm) {
            return PinnedSelection{*originIcao, PinnedKind::kOrigin};
        }
        return PinnedSelection{*destinationIcao, PinnedKind::kDestination};
    }
    if (originIcao) {
        return PinnedSelection{*originIcao, PinnedKind::kOrigin};
    }
    if (destinationIcao) {
        return PinnedSelection{*destinationIcao, PinnedKind::kDestination};
    }
    return std::nullopt;
}

std::optional<PinnedEntryResult> BuildPinnedEntry(const std::optional<std::string>& originIcao,
                                                   const std::optional<std::string>& destinationIcao,
                                                   const AirportDatabase& db, double userLatDeg, double userLonDeg,
                                                   const SightingTracker& sightingTracker,
                                                   const AirportEntryInputs& inputs, double nowSec,
                                                   const AggregatorConfig& config)
{
    std::optional<double> originDistanceNm;
    if (originIcao && destinationIcao) {
        originDistanceNm = AirportDistanceNm(db, *originIcao, userLatDeg, userLonDeg);
    }

    const std::optional<PinnedSelection> selection =
        SelectPinnedAirport(originIcao, destinationIcao, originDistanceNm, config.origin_pin_radius_nm);
    if (!selection) {
        return std::nullopt;
    }

    const std::optional<double> distanceNm = AirportDistanceNm(db, selection->icao, userLatDeg, userLonDeg);
    const auto airportIt = db.find(selection->icao);
    const Airport* airport = (airportIt != db.end()) ? &airportIt->second : nullptr;

    AirportEntry entry = BuildAirportEntry(selection->icao, distanceNm, airport, sightingTracker, inputs, nowSec, config);
    return PinnedEntryResult{std::move(entry), selection->kind};
}

} // namespace trm::core
