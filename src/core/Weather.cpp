#include "core/Weather.h"

#include <limits>

namespace trm::core {

std::optional<WindLayer> PickLowestAltitudeWindLayer(const std::vector<WindLayer>& layers)
{
    const WindLayer* lowest = nullptr;
    float lowestAlt = std::numeric_limits<float>::infinity();
    for (const WindLayer& layer : layers) {
        if (layer.altitude_msl_m < lowestAlt) {
            lowest = &layer;
            lowestAlt = layer.altitude_msl_m;
        }
    }
    if (!lowest) {
        return std::nullopt;
    }
    return *lowest;
}

} // namespace trm::core
