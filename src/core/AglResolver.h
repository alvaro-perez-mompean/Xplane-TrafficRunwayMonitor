#pragma once

#include <optional>

// Picks the AGL reference to trust: a real terrain-probe reading
// (sdk::TerrainProbe) or the cheap airport-elevation approximation
// Plugin.cpp used exclusively before this module existed.
//
// This is the one genuinely branchy piece of the terrain-probe feature, so
// it lives in core/ per this project's own architecture rule -- everything
// else touching it (sdk::TerrainProbe, Plugin.cpp) is thin glue or wiring.

namespace trm::core {

enum class AglSource { kTerrainProbe, kAirportElevation };

struct AglResolverConfig {
    // XPLMScenery.h documents the Y-testing API as limited to the loaded
    // scenery area (~300x300km); a probe outside that area still reports
    // xplm_ProbeHitTerrain -- a normal "success" result -- but at a bogus
    // 0 MSL sphere. That failure mode can't be caught from the probe's own
    // result code, so instead: reject a probe reading that disagrees with
    // the airport-elevation baseline by more than this many meters, since
    // real terrain near a matched/nearby airport should never be this far
    // off from that airport's own charted elevation.
    //
    // Also the only guard against a second, real (not-outside-loaded-
    // scenery) failure mode found via in-sim testing at LEMD: a custom
    // orthophoto/terrain-mesh scenery pack whose airport flattening didn't
    // cover that field, so the probe hits genuine but unflattened terrain
    // ~100m below the actual pavement -- a "successful" reading that's
    // still wrong. Observed disagreement there clustered tightly around
    // 99-101m for stationary traffic, so this needs to sit well under 100m
    // to reject that case rather than the old 900m (sized only for the
    // out-of-loaded-scenery case above, which typically disagrees by far
    // more).
    double max_disagreement_m = 75.0; // ~250 ft
};

struct AglResult {
    double agl_m = 0.0;
    AglSource source = AglSource::kAirportElevation;
};

// `aircraftMslM` is the aircraft's own altitude, meters MSL.
// `probeElevationMslM` is sdk::TerrainProbe's raw result for this cycle, or
// nullopt whenever the probe wasn't run at all (throttled -- see Plugin.cpp)
// or came back xplm_ProbeMissed/xplm_ProbeError. `airportElevationFt` is the
// matched-or-nearest airport's charted elevation, always available whenever
// the caller has a reference airport at all -- the same value Plugin.cpp
// used alone before this module existed.
AglResult ResolveAgl(double aircraftMslM, std::optional<double> probeElevationMslM, double airportElevationFt,
                      const AglResolverConfig& config = {});

} // namespace trm::core
