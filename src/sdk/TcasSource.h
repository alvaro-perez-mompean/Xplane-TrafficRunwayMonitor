#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/SlotValidity.h"

// Real XPLM SDK glue for reading traffic positions: TCAS Override (up to 63
// AI aircraft) when available, else the legacy 19-slot multiplayer mirror.
// Calls the real SDK functions directly, with no availability-probing dance.
//
// Thin glue, not core-testable (real XPLMDataRef/XPLMLocalToWorld calls);
// the validity/stale-slot logic it depends on lives in core::SlotValidity so
// at least that piece is unit-tested.
//
// Note on "icao" downstream (SightingTracker): raw TCAS/legacy
// slots carry no aircraft identity at all, just position -- slot_index is
// the distinct-aircraft proxy for the whole tracked session (as long as the
// same slot isn't recycled to a different real aircraft; see
// core::SlotValidity).

namespace trm::sdk {

// One slot's reading for this cycle. Sized/indexed 1..SlotCount() every
// cycle so callers can
// track valid -> invalid transitions per slot index without extra
// bookkeeping of their own.
struct SlotReading {
    int slot_index = 0; // 1-based
    bool valid = false;
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double msl_m = 0.0;
    double heading_true_deg = 0.0;
    double gs_kt = 0.0;  // sqrt(vx^2 + vz^2), knots
    double vs_mps = 0.0; // vy directly, positive = up

    // Empty for this source -- TCAS Override / legacy multiplayer datarefs
    // carry no aircraft identity at all (see this file's own top comment).
    // Only sdk::LtapiSource ever populates this, from LTAPIAircraft's own
    // getCallSign().
    std::string callsign;

    // Always false for this source -- TCAS Override / legacy multiplayer
    // datarefs carry no aircraft type/category info at all, same blind spot
    // as callsign above. Only sdk::LtapiSource ever sets this true, from
    // LTAPIAircraft::getAcClass()'s DOC 8643 type-of-aircraft code ('H' for
    // helicopter). Runway matching/phase classification (RunwayMatcher,
    // PhaseClassifier) assume fixed-wing traffic-pattern behavior --
    // heading-aligned approach, decelerating through touchdown -- which
    // doesn't hold for rotorcraft (hover, vertical/off-centerline landings),
    // so callers should skip that pipeline entirely whenever this is true.
    bool is_helicopter = false;
};

class TcasSource {
public:
    TcasSource();
    // Declared (not defaulted inline) because TcasDataRefs/LegacySlotDataRefs
    // are only forward-declared here -- an implicit inline destructor would
    // need their complete types wherever a TcasSource is first destroyed,
    // which breaks as soon as that happens outside TcasSource.cpp (e.g. a
    // std::unique_ptr<TcasSource> owned by Plugin.cpp's orchestration code).
    // Defined out-of-line in TcasSource.cpp, where both types are complete.
    ~TcasSource();

    // 63 if the TCAS Override datarefs resolved at construction, else 19
    // (the legacy multiplayer mirror's fixed size).
    int SlotCount() const { return slot_count_; }
    bool HasExtendedTraffic() const { return has_extended_traffic_; }

    // Reads every slot for this cycle. `nowSec` drives the stale-slot grace
    // period (core::SlotValidity) -- pass a monotonic wall-clock seconds
    // value, consistent cycle to cycle (e.g. XPLMGetElapsedTime()).
    std::vector<SlotReading> CollectTraffic(double nowSec);

private:
    struct TcasDataRefs;
    struct LegacySlotDataRefs;

    bool has_extended_traffic_ = false;
    int slot_count_ = 0;

    std::unique_ptr<TcasDataRefs> tcas_refs_;
    std::vector<LegacySlotDataRefs> legacy_refs_; // index 0 unused, 1..19 used

    std::vector<core::SlotHistory> slot_history_; // index 0 unused, 1..slot_count_ used
};

} // namespace trm::sdk
