#include "sdk/TerrainProbe.h"

#include "XPLMGraphics.h"

namespace trm::sdk {

TerrainProbe::~TerrainProbe()
{
    for (auto& [slotIndex, probe] : probes_) {
        XPLMDestroyProbe(probe);
    }
}

std::optional<double> TerrainProbe::ProbeElevationM(int slotIndex, double latDeg, double lonDeg, double aircraftMslM)
{
    XPLMProbeRef& probe = probes_[slotIndex];
    if (probe == nullptr) {
        probe = XPLMCreateProbe(xplm_ProbeY);
    }

    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    XPLMWorldToLocal(latDeg, lonDeg, aircraftMslM, &x, &y, &z);

    XPLMProbeInfo_t info;
    info.structSize = sizeof(XPLMProbeInfo_t);
    const XPLMProbeResult result =
        XPLMProbeTerrainXYZ(probe, static_cast<float>(x), static_cast<float>(y), static_cast<float>(z), &info);

    if (result != xplm_ProbeHitTerrain) {
        return std::nullopt;
    }
    // Local Y in X-Plane's local (OpenGL-derived) coordinate system is
    // meters MSL by construction -- see XPLMWorldToLocal's own doc
    // ("altitude is in meters MSL... XYZ coordinates are in meters in the
    // local OpenGL coordinate system") -- so locationY needs no further
    // conversion back through XPLMLocalToWorld.
    return static_cast<double>(info.locationY);
}

void TerrainProbe::ClearSlot(int slotIndex)
{
    auto it = probes_.find(slotIndex);
    if (it != probes_.end()) {
        XPLMDestroyProbe(it->second);
        probes_.erase(it);
    }
}

} // namespace trm::sdk
