#pragma once

#include <string>
#include <unordered_map>
#include <vector>

// Stable small-integer slot assignment for LTAPI's bulk-data source.
//
// livetraffic/bulk/quick's array order is "whichever aircraft LiveTraffic
// is currently displaying" -- LTAPI's own docs don't promise array index N
// stays attached to the same physical aircraft between polls, unlike TCAS
// Override's arrays (core::SlotValidity's own stale-slot handling assumes
// slot stability, which TCAS Override provides but LTAPI's key order does
// not). The rest of this pipeline's per-slot state (TrendFilter hysteresis,
// SightingTracker's confirm-before/confirm-after marks) assumes slot i
// means the same real aircraft from one cycle to the next -- violating
// that silently would be exactly the class of bug core::SlotValidity
// exists to prevent for the TCAS/legacy path. So each of LTAPI's own
// stable per-aircraft keys (LTAPIAircraft::getKey()) is mapped here to its
// own small integer slot, held for as long as that key keeps appearing,
// and only handed to a *different* key after at least one full cycle where
// it read as absent -- that gap is what lets a slot's "went invalid this
// cycle" cleanup run before the slot number could be silently reused for a
// different aircraft in the same cycle.
//
// Pure logic, no XPLM/LTAPI dependency -- unit-testable directly.

namespace trm::core {

class SlotAssigner {
public:
    // `maxSlots` is a soft pre-sizing ceiling, not a real platform limit
    // (unlike TCAS Override's genuine 63-aircraft cap) -- if ever
    // exceeded, the overflowing keys simply get no slot this cycle (see
    // AssignSlots's return value) rather than corrupting an existing
    // assignment.
    explicit SlotAssigner(int maxSlots = 250);

    // Assigns/reuses stable slots (1..maxSlots) for this cycle's set of
    // distinct keys. Returns key -> slot for every key that got one;
    // a key not present in the returned map means the soft ceiling was
    // exceeded this cycle (see HasOverflowed()). A key missing from
    // `keysThisCycle` that held a slot in a prior call has that slot
    // queued for reuse starting on the *next* call, not this one --
    // giving callers exactly one full cycle to notice "this slot went
    // invalid" before the slot index could mean a different aircraft.
    std::unordered_map<std::string, int> AssignSlots(const std::vector<std::string>& keysThisCycle);

    // True once the soft ceiling has been exceeded at least once across
    // the lifetime of this assigner (a one-time latch, not per-cycle).
    bool HasOverflowed() const { return overflowed_; }

private:
    int max_slots_;
    bool overflowed_ = false;

    std::unordered_map<std::string, int> key_to_slot_;
    std::unordered_map<int, std::string> slot_to_key_;
    std::vector<int> free_slots_;         // slots safe to reuse now
    std::vector<int> pending_free_slots_; // slots freed this cycle; promoted next cycle
    int next_new_slot_ = 0;
};

} // namespace trm::core
