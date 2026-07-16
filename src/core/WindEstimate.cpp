#include "core/WindEstimate.h"
#include "core/GeoMath.h"

#include <cmath>
#include <limits>

namespace trm::core {

std::optional<WindInfo> ResolveEffectiveWind(const std::optional<WindReading>& airportPositionReading,
                                              const std::optional<WindReading>& aircraftPositionReading,
                                              const WindEstimateConfig& config)
{
    std::optional<WindReading> selected;
    WindEstimateSource source = WindEstimateSource::kRegional;

    // Airport-position reading is tried first, regardless of its speed
    // (even a dead calm real reading is trusted over the fallback below).
    if (airportPositionReading) {
        selected = airportPositionReading;
        source = airportPositionReading->has_station_match ? WindEstimateSource::kStation
                                                             : WindEstimateSource::kRegional;
    } else if (aircraftPositionReading) {
        selected = aircraftPositionReading;
        source = WindEstimateSource::kAircraftPosition;
    }

    if (!selected) {
        return std::nullopt;
    }

    WindInfo info;
    info.speed_kt = selected->speed_kt;
    info.direction_true_deg = selected->direction_true_deg;
    info.source = source;
    info.is_calm = selected->speed_kt < config.min_speed_kt;
    return info;
}

std::optional<WindEstimateResult> EstimateWindFavoredRunwayEnd(const Airport& airport,
                                                                 const std::optional<WindReading>& airportPositionReading,
                                                                 const std::optional<WindReading>& aircraftPositionReading,
                                                                 const WindEstimateConfig& config)
{
    const std::optional<WindInfo> wind = ResolveEffectiveWind(airportPositionReading, aircraftPositionReading, config);
    if (!wind || wind->is_calm) {
        return std::nullopt;
    }

    const RunwayEnd* best = nullptr;
    double bestDiff = std::numeric_limits<double>::infinity();
    for (const RunwayEnd& rwyEnd : airport.runways) {
        const double diff = std::abs(AngleDiffDeg(wind->direction_true_deg, rwyEnd.heading_deg));
        if (diff < bestDiff) {
            best = &rwyEnd;
            bestDiff = diff;
        }
    }

    if (!best) {
        return std::nullopt;
    }

    return WindEstimateResult{best->id, wind->source};
}

WindEstimateSource UpgradeToOwnStationIfConfirmed(WindEstimateSource source, bool metarAvailableForThisAirport)
{
    if (source == WindEstimateSource::kStation && metarAvailableForThisAirport) {
        return WindEstimateSource::kOwnStation;
    }
    return source;
}

std::string WindEstimateSourceLabel(WindEstimateSource source)
{
    switch (source) {
        case WindEstimateSource::kOwnStation:
            return "this airport's own station";
        case WindEstimateSource::kStation:
            return "real weather data nearby";
        case WindEstimateSource::kRegional:
            return "regional weather estimate";
        case WindEstimateSource::kAircraftPosition:
            return "your aircraft's position";
    }
    return "an estimate";
}

} // namespace trm::core
