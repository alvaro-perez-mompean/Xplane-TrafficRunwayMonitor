#pragma once

#include <optional>
#include <unordered_map>

#include "XPLMScenery.h"

// Real XPLM SDK glue for terrain-height probing (XPLMProbeTerrainXYZ) --
// core/AglResolver.h is the pure sanity-check that decides whether this
// module's own result is trusted over the airport-elevation approximation.
//
// One persistent XPLMProbeRef per tracked slot index, created once and
// reused cycle-to-cycle: XPLMScenery.h's own performance guidance is
// explicit that probing is expensive and should be cached rather than
// allocated fresh per call.
//
// Thin glue, not core-testable (real XPLMWorldToLocal/XPLMProbeTerrainXYZ
// calls) -- matches sdk::AptDatLoader's own untested status.

namespace trm::sdk {

class TerrainProbe {
public:
    TerrainProbe() = default;
    ~TerrainProbe();

    TerrainProbe(const TerrainProbe&) = delete;
    TerrainProbe& operator=(const TerrainProbe&) = delete;

    // Probes terrain elevation (meters MSL) under (latDeg, lonDeg), reusing
    // slotIndex's own persistent probe (created on first use for that slot).
    // aircraftMslM only seeds the query point's altitude for the
    // XPLMWorldToLocal conversion -- the probe itself finds the tallest
    // terrain along that vertical column regardless of the altitude passed
    // in.
    //
    // Returns nullopt on xplm_ProbeMissed/xplm_ProbeError. NOT a reliable
    // "out of range" signal by itself -- XPLMScenery.h documents that a
    // probe outside the ~300x300km loaded scenery area still reports
    // xplm_ProbeHitTerrain (a "success"), just at a bogus 0 MSL sphere.
    // core::ResolveAgl's disagreement-threshold check is what actually
    // catches that case, not this return type.
    std::optional<double> ProbeElevationM(int slotIndex, double latDeg, double lonDeg, double aircraftMslM);

    // Releases slotIndex's probe, if one exists. Call when a slot goes
    // invalid (mirrors SightingTracker::ClearSlotState) -- not a
    // correctness requirement (a reused probe handle works fine queried
    // against a different aircraft's position too), just avoids holding a
    // probe open for a slot that may never become valid again this session.
    void ClearSlot(int slotIndex);

private:
    std::unordered_map<int, XPLMProbeRef> probes_;
};

} // namespace trm::sdk
