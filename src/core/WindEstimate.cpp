#include "core/WindEstimate.h"
#include "core/ActiveRunway.h"

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

std::optional<RunwayEstimate> EstimateWindFavoredRunwayEnd(const Airport& airport,
                                                                 const std::optional<WindReading>& airportPositionReading,
                                                                 const std::optional<WindReading>& aircraftPositionReading,
                                                                 const WindEstimateConfig& config)
{
    const std::optional<WindInfo> wind = ResolveEffectiveWind(airportPositionReading, aircraftPositionReading, config);
    if (!wind || wind->is_calm) {
        return std::nullopt;
    }

    // Delegates to the crosswind tier of core::SelectActiveRunway so there is
    // one runway-picking implementation, not two that can drift apart. This
    // function's own remaining job is the reading-selection half above, plus
    // tagging the result with which reading won.
    // This function has already gated on its own is_calm above, so hand the
    // crosswind tier the same threshold rather than let its independent (and
    // higher) default re-classify a 2kt wind as calm and skip straight to the
    // tie-break.
    ActiveRunwayConfig activeRunwayConfig;
    activeRunwayConfig.calm_wind_kt = config.min_speed_kt;

    const std::optional<std::string> runwayId =
        SelectCrosswindFavoredRunway(airport, wind->direction_true_deg, wind->speed_kt, activeRunwayConfig);
    if (!runwayId) {
        return std::nullopt;
    }

    return RunwayEstimate{*runwayId, wind->source};
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
