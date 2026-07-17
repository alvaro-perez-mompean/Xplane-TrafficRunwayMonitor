#include "core/AglResolver.h"

#include <cmath>

namespace trm::core {

AglResult ResolveAgl(double aircraftMslM, std::optional<double> probeElevationMslM, double airportElevationFt,
                      const AglResolverConfig& config)
{
    const double airportElevationMslM = airportElevationFt * 0.3048;

    if (probeElevationMslM.has_value() &&
        std::abs(*probeElevationMslM - airportElevationMslM) <= config.max_disagreement_m) {
        return AglResult{aircraftMslM - *probeElevationMslM, AglSource::kTerrainProbe};
    }

    return AglResult{aircraftMslM - airportElevationMslM, AglSource::kAirportElevation};
}

} // namespace trm::core
