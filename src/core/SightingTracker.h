#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "core/PhaseClassifier.h"

// Confirm-before/confirm-after sighting tracking. This is the
// highest-risk module in the whole codebase: several real false-positive
// sightings have been found and fixed via in-sim testing -- be careful
// before changing anything here.
//
// No score, no decay: this just accumulates timestamped sightings per
// (icao, category, runway_id), where `icao` is the *matched airport's* ICAO
// (raw TCAS/legacy slots carry no aircraft identity at all -- slot_index is
// the distinct-aircraft proxy for as long as a slot isn't recycled to a
// different real aircraft; see core::SlotValidity). Aggregation (section
// 4.6, a later module) is what turns this into "active"/"history" answers.

namespace trm::core {

enum class SightingCategory { kArrival, kDeparture };

struct SightingConfig {
    double departure_confirm_window_sec = 180.0; // 3 minutes
    double arrival_confirm_window_sec = 300.0;    // 5 minutes

    // Consecutive final_approach cycles the SAME (icao, runway_id) must be
    // seen before it's trusted enough to become/replace pending_arrival.
    // RunwayMatcher is a fresh per-cycle geometry snapshot with no
    // continuity check of its own (see its own header comment) -- without
    // this, a single-cycle coincidental match (e.g. transiting past a
    // small airport during a turn, confirmed in real telemetry to happen
    // between closely-spaced fields) could set up a false arrival
    // confirmation, or silently overwrite an already-established approach.
    int final_approach_confirm_cycles = 3;
};

// One already-confirmed sighting, returned by ProcessSlot exactly once per
// new confirmation -- NOT repeated every cycle the confirming phase holds
// (e.g. several consecutive kInitialClimb cycles for the same climb-out).
// Feeds core::EventLog for the UI's History tab; time_sec is the same
// monotonic clock as the rest of this module (XPLMGetElapsedTime via the
// caller), not wall-clock time. callsign is empty whenever the traffic
// source that observed this slot doesn't carry aircraft identity (TCAS
// Override / legacy multiplayer -- see sdk::SlotReading's own comment);
// only LTAPI-sourced traffic ever populates it.
struct RunwayEvent {
    std::string icao;
    SightingCategory category;
    std::string runway_id;
    double time_sec = 0.0;
    std::string callsign;
};

// A ground_sighting or pending_arrival mark.
struct SightingMark {
    std::string icao;
    std::string runway_id;
    double time_sec = 0.0;
};

// Per-slot confirmation state the caller owns across cycles, one instance
// per tracked slot index.
struct SlotSightingState {
    std::optional<SightingMark> ground_sighting;
    std::optional<SightingMark> pending_arrival;

    // Hysteresis bookkeeping for pending_arrival (see ProcessSlot and
    // SightingConfig::final_approach_confirm_cycles) -- tracks how many
    // consecutive final_approach cycles the current (icao, runway_id)
    // candidate has been seen, independent of whatever's already recorded
    // in pending_arrival above.
    std::string pending_arrival_candidate_icao;
    std::string pending_arrival_candidate_runway_id;
    int pending_arrival_candidate_streak = 0;
};

// slot_index -> last-seen time (seconds); one entry per distinct
// contributing aircraft (keyed by slot index, not incremented per
// observation, so this counts distinct aircraft rather than raw ~1Hz
// samples).
using ContributorMap = std::unordered_map<int, double>;
// runway_id -> contributors, for one (icao, category).
using RunwaySightings = std::unordered_map<std::string, ContributorMap>;

class SightingTracker {
public:
    explicit SightingTracker(SightingConfig config = {});

    // Per-cycle input for one tracked slot: its matched airport/runway (if
    // any, from RunwayMatcher -- empty icao/runway_id means "not matched
    // this cycle") and its current classified phase (PhaseClassifier).
    struct SlotObservation {
        std::string icao;
        std::string runway_id;
        std::string other_end_id; // this runway end's physical reciprocal
        FlightPhase phase = FlightPhase::kAirborneEnroute;
        std::string callsign; // empty if this traffic source carries no aircraft identity (sdk::SlotReading)
    };

    // Updates `slotState` in place, and may
    // record a confirmed sighting into this tracker's own store -- returns
    // that sighting as a RunwayEvent the one time it's newly confirmed,
    // nullopt otherwise (including every subsequent cycle the same
    // confirmation just keeps refreshing this tracker's own contributor
    // map).
    std::optional<RunwayEvent> ProcessSlot(int slotIndex, SlotSightingState& slotState,
                                            const SlotObservation& observation, double nowSec);

    // Call when a previously-valid slot goes invalid this cycle (e.g. it
    // disappeared from the TCAS array): the underlying slot index can be
    // recycled for a completely different real aircraft, and stale
    // ground_sighting/pending_arrival would otherwise silently misattribute
    // a confirmation to it.
    void ClearSlotState(SlotSightingState& slotState) const;

    // `maxAgeSec` should be (active window * history window multiplier),
    // computed fresh by the caller on every call -- the active window is
    // user-adjustable at runtime, so this deliberately isn't cached
    // internally.
    void PruneStaleSightings(double nowSec, double maxAgeSec);

    // Read-only query for one (icao, category) -- runway_id -> contributors,
    // for Aggregator to query active/history windows over.
    // Returns nullptr if there's no entry at all for this icao (never
    // creates one as a side effect, unlike the write path below).
    const RunwaySightings* FindSightings(const std::string& icao, SightingCategory category) const;

private:
    struct AirportSightings {
        RunwaySightings arrival;
        RunwaySightings departure;
    };

    RunwaySightings& CategoryMapFor(const std::string& icao, SightingCategory category);

    // Records one already-confirmed sighting and invalidates the physical
    // reciprocal runway end in both categories (a runway operates in one
    // direction at a time for all traffic -- a hard physical correction,
    // not a time-based fade, so it's instant). Returns true the first time
    // this (icao, category, runwayId) sees this particular slotIndex --
    // false on every subsequent refresh of the same contributor, which
    // ProcessSlot uses to only surface a RunwayEvent once per confirmation.
    bool RecordSighting(const std::string& icao, SightingCategory category, const std::string& runwayId,
                        const std::string& otherEndId, int slotIndex, double nowSec);
    void InvalidateRunwayEnd(const std::string& icao, const std::string& runwayId);

    SightingConfig config_;
    std::unordered_map<std::string, AirportSightings> sightings_; // keyed by airport icao
};

} // namespace trm::core
