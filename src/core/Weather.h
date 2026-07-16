#pragma once

#include <optional>
#include <vector>

// Pure logic supporting sdk/Weather's region-array wind fallback.

namespace trm::core {

struct WindLayer {
    float altitude_msl_m = 0.0f;
    float speed_kt = 0.0f;
    float direction_true_deg = 0.0f;
};

// Picks the layer with the lowest MSL altitude -- the closest available
// approximation to surface-level wind, since layer ordering isn't
// documented as fixed. Returns nullopt if `layers` is empty.
std::optional<WindLayer> PickLowestAltitudeWindLayer(const std::vector<WindLayer>& layers);

} // namespace trm::core
