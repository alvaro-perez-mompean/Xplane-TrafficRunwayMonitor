#include "core/Aggregator.h"

#include <algorithm>

namespace trm::core {

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

    WindEstimateConfig windConfig;
    windConfig.min_speed_kt = config.wind_estimate_min_speed_kt;

    entry.current_wind =
        ResolveEffectiveWind(inputs.wind_airport_position_reading, inputs.wind_aircraft_position_reading, windConfig);

    if (inputs.wind_airport_position_reading.has_value()) {
        entry.altimeter_pa = inputs.wind_airport_position_reading->pressure_pa;
    }

    if (airport && (entry.arrivals.NeedsWindEstimate() || entry.departures.NeedsWindEstimate())) {
        entry.wind_estimate = EstimateWindFavoredRunwayEnd(*airport, inputs.wind_airport_position_reading,
                                                             inputs.wind_aircraft_position_reading, windConfig);
    }

    entry.metar = inputs.metar;

    if (entry.wind_estimate) {
        entry.wind_estimate->source =
            UpgradeToOwnStationIfConfirmed(entry.wind_estimate->source, entry.metar.has_value());
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
