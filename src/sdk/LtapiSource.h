#pragma once

#include "LTAPI.h"

#include "core/SlotAssignment.h"
#include "sdk/TcasSource.h"

// Real LTAPI glue: LiveTraffic's own bulk-data C++ API
// (LTAPIConnect::UpdateAcList()), not capped at TCAS Override's 63-aircraft
// limit -- bounded only by LiveTraffic's own max_num_ac setting.
//
// Confirmed in-sim that this exact API -- LTAPIConnect::UpdateAcList(),
// called from a real compiled plugin -- reliably returns real data. This
// uses LTAPI's own maintained library code rather than a hand-rolled
// dataref-level read path, so it's always preferred when available (see
// the `else if` fallback to TCAS Override in the caller).
//
// Struct-version differences (LTAPI.h's own v4.1.2-vs-v4.4.0 struct size
// negotiation) are already handled internally by LTAPIConnect/
// LTAPIAircraft; getLat()/getLon()/getAltFt() work unconditionally
// regardless of which struct size this LiveTraffic build actually
// compiles, so no fallback logic is needed here at all.
//
// Thin XPLM/LTAPI glue, not unit-tested -- the one piece of real logic it
// depends on (stable keyNum -> slot assignment) lives in
// core::SlotAssigner, which is.

namespace trm::sdk {

class LtapiSource {
public:
    // `maxSlots` is a soft pre-sizing ceiling (see core::SlotAssigner),
    // not a real platform limit -- 250 comfortably covers any realistic
    // LiveTraffic max_num_ac setting.
    explicit LtapiSource(int maxSlots = 250);

    int MaxSlots() const { return max_slots_; }

    // True if LiveTraffic is installed, running, and actively displaying
    // aircraft. Checked live every call (matches LTAPIConnect's own
    // documented advice not to assume this is stable across a session) --
    // callers should fall back to another traffic source for any cycle
    // this returns false.
    bool IsAvailable() const;

    // Polls LiveTraffic and returns every slot's reading for this cycle,
    // sized MaxSlots()+1 (index 0 unused), matching sdk::TcasSource's own
    // convention so both sources can feed the same per-slot orchestration
    // loop identically. Unlike TcasSource, no staleness/validity filtering
    // is needed here: LTAPI's bulk array is documented to only ever
    // contain aircraft LiveTraffic is actively displaying, so there's no
    // "frozen empty slot" sentinel to guard against.
    std::vector<SlotReading> CollectTraffic();

private:
    LTAPIConnect connect_;
    core::SlotAssigner slot_assigner_;
    int max_slots_;
};

} // namespace trm::sdk
