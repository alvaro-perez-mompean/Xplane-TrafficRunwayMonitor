#include <catch2/catch_test_macros.hpp>

#include "core/SightingTracker.h"

using namespace trm::core;

namespace {

// Missing entry and present-but-empty entry both mean "zero distinct
// contributors" -- InvalidateRunwayEnd/PruneStaleSightings may leave an
// empty map behind rather than erasing the runway_id key outright.
size_t ContributorCount(const SightingTracker& tracker, const std::string& icao, SightingCategory category,
                         const std::string& runwayId)
{
    const RunwaySightings* sightings = tracker.FindSightings(icao, category);
    if (!sightings) {
        return 0;
    }
    const auto it = sightings->find(runwayId);
    return (it == sightings->end()) ? 0 : it->second.size();
}

} // namespace

TEST_CASE("SightingTracker: real departure sequence records exactly once", "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTakeoffRoll}, 10.0);
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 40.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "09") == 0);
}

TEST_CASE("SightingTracker: initial_climb alone (no prior ground_sighting) records nothing", "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 0.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 0);
}

TEST_CASE("SightingTracker: final_approach alone records nothing and expires cleanly", "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    // Default final_approach_confirm_cycles is 3 -- the same (icao,
    // runway_id) must be seen this many consecutive cycles before
    // pending_arrival is actually set.
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 1.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 2.0);
    REQUIRE(slot.pending_arrival.has_value());

    // No touchdown ever follows -- window (default 300s, counted from when
    // pending_arrival was actually set at t=2.0) expires, then a
    // touchdown-shaped phase arrives too late to confirm anything.
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kLandingRollout}, 303.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "27") == 0);
    // Cleared regardless of whether it matched.
    CHECK_FALSE(slot.pending_arrival.has_value());
}

TEST_CASE("SightingTracker regression: a brief flicker to a different nearby airport's final_approach "
          "must not overwrite an established pending_arrival",
          "[SightingTracker]")
{
    // Reproduces the LELL/XLE001R (Sabadell/Sant Cugat) pattern observed in
    // real telemetry: two small airfields close enough together that
    // RunwayMatcher's per-cycle geometry snapshot can flip between them
    // during a turn, purely because it has no continuity check of its own.
    SightingTracker tracker;
    SlotSightingState slot;

    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 1.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 2.0);
    REQUIRE(slot.pending_arrival.has_value());
    REQUIRE(slot.pending_arrival->icao == "KTST");

    // One-cycle flicker onto a different nearby airport's matching cone --
    // shorter than final_approach_confirm_cycles (default 3), so it must
    // not touch the already-established KTST pending_arrival.
    tracker.ProcessSlot(1, slot, {"KZZZ", "18", "36", FlightPhase::kFinalApproach}, 3.0);
    CHECK(slot.pending_arrival->icao == "KTST");

    // Flicker ends, real approach resumes and confirms at KTST as expected.
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 4.0);
    const auto event = tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kLandingRollout}, 5.0);
    REQUIRE(event.has_value());
    CHECK(event->icao == "KTST");
    CHECK(ContributorCount(tracker, "KZZZ", SightingCategory::kArrival, "18") == 0);
}

TEST_CASE("SightingTracker: a final_approach switch sustained for confirm_cycles DOES replace "
          "pending_arrival",
          "[SightingTracker]")
{
    // The flip side of the flicker-protection test above: a genuinely
    // sustained switch to a different airport (not a one-cycle blip) is
    // still trusted, matching a real change of intent (e.g. a go-around
    // followed by a real approach elsewhere).
    SightingTracker tracker;
    SlotSightingState slot;

    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 1.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 2.0);
    REQUIRE(slot.pending_arrival->icao == "KTST");

    tracker.ProcessSlot(1, slot, {"KZZZ", "18", "36", FlightPhase::kFinalApproach}, 3.0);
    tracker.ProcessSlot(1, slot, {"KZZZ", "18", "36", FlightPhase::kFinalApproach}, 4.0);
    tracker.ProcessSlot(1, slot, {"KZZZ", "18", "36", FlightPhase::kFinalApproach}, 5.0);
    CHECK(slot.pending_arrival->icao == "KZZZ");

    const auto event = tracker.ProcessSlot(1, slot, {"KZZZ", "18", "36", FlightPhase::kLandingRollout}, 6.0);
    REQUIRE(event.has_value());
    CHECK(event->icao == "KZZZ");
}

TEST_CASE("SightingTracker: real arrival sequence records exactly once", "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 10.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 20.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kLandingRollout}, 30.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "27") == 1);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "27") == 0);
}

TEST_CASE("SightingTracker: reciprocal invalidation fires on the opposite end", "[SightingTracker]")
{
    SightingTracker tracker;

    // Seed sightings on runway 27 in both categories via two different slots.
    SlotSightingState slotDep27;
    tracker.ProcessSlot(5, slotDep27, {"KTST", "27", "09", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(5, slotDep27, {"KTST", "27", "09", FlightPhase::kTakeoffRoll}, 10.0);
    tracker.ProcessSlot(5, slotDep27, {"KTST", "27", "09", FlightPhase::kInitialClimb}, 40.0);

    SlotSightingState slotArr27;
    tracker.ProcessSlot(6, slotArr27, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(6, slotArr27, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 10.0);
    tracker.ProcessSlot(6, slotArr27, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 20.0);
    tracker.ProcessSlot(6, slotArr27, {"KTST", "27", "09", FlightPhase::kLandingRollout}, 30.0);

    REQUIRE(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "27") == 1);
    REQUIRE(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "27") == 1);

    // Now a confirmed departure on the reciprocal end (09) must clear 27 in
    // both categories, instantly.
    SlotSightingState slotDep09;
    tracker.ProcessSlot(7, slotDep09, {"KTST", "09", "27", FlightPhase::kTaxi}, 100.0);
    tracker.ProcessSlot(7, slotDep09, {"KTST", "09", "27", FlightPhase::kTakeoffRoll}, 110.0);
    tracker.ProcessSlot(7, slotDep09, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 140.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "27") == 0);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "27") == 0);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1);
}

TEST_CASE("SightingTracker regression: a landing rollout's spurious takeoff_roll-shaped "
          "moment must not log a departure",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    // Real arrival in progress: on final, sets pending_arrival.
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 10.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 20.0);

    // Upstream phase classification briefly (and wrongly) reads this same
    // rollout as takeoff_roll for one cycle -- takeoff_roll only ever
    // refreshes ground_sighting, never records by itself.
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kTakeoffRoll}, 25.0);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "27") == 0);

    // The real classification resumes and confirms the arrival.
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kLandingRollout}, 30.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "27") == 1);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "27") == 0);
}

TEST_CASE("SightingTracker regression: a departure's spurious landing_rollout-shaped "
          "moment must not log an arrival",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    // Real departure ground roll: sets ground_sighting.
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTakeoffRoll}, 0.0);

    // Upstream phase classification briefly (and wrongly) reads this same
    // ground roll as landing_rollout for one cycle. There was never a prior
    // final_approach, so pending_arrival is empty -- nothing to confirm.
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kLandingRollout}, 5.0);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "09") == 0);

    // The real climb-out confirms the departure via the refreshed
    // ground_sighting (from either cycle above -- both refresh it).
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 35.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "09") == 0);
}

TEST_CASE("SightingTracker: departure confirm window boundary", "[SightingTracker]")
{
    SightingConfig config;
    config.departure_confirm_window_sec = 180.0;

    SECTION("within window: confirms") {
        SightingTracker tracker(config);
        SlotSightingState slot;
        tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
        tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 180.0);
        CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1);
    }
    SECTION("past window: does not confirm") {
        SightingTracker tracker(config);
        SlotSightingState slot;
        tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
        tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 180.1);
        CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 0);
    }
}

TEST_CASE("SightingTracker: ClearSlotState drops both marks", "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 1.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 2.0);
    tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 3.0);
    REQUIRE(slot.ground_sighting.has_value());
    REQUIRE(slot.pending_arrival.has_value());

    tracker.ClearSlotState(slot);

    CHECK_FALSE(slot.ground_sighting.has_value());
    CHECK_FALSE(slot.pending_arrival.has_value());
}

TEST_CASE("SightingTracker: ProcessSlot returns a departure RunwayEvent exactly once per confirmation",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    CHECK_FALSE(tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0).has_value());

    const auto event = tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 10.0);
    REQUIRE(event.has_value());
    CHECK(event->icao == "KTST");
    CHECK(event->runway_id == "09");
    CHECK(event->category == SightingCategory::kDeparture);
    CHECK(event->time_sec == 10.0);
    CHECK(event->callsign.empty()); // no callsign supplied by this observation

    // Same aircraft still climbing next cycle, still within window -- this
    // is the same confirmed sighting refreshing its contributor timestamp,
    // not a new one, so it must not surface a second event.
    CHECK_FALSE(tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 11.0).has_value());
}

TEST_CASE("SightingTracker: ProcessSlot returns an arrival RunwayEvent on confirmed touchdown", "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    CHECK_FALSE(tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 0.0).has_value());
    CHECK_FALSE(tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 10.0).has_value());
    CHECK_FALSE(tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kFinalApproach}, 20.0).has_value());

    const auto event = tracker.ProcessSlot(1, slot, {"KTST", "27", "09", FlightPhase::kLandingRollout}, 30.0);
    REQUIRE(event.has_value());
    CHECK(event->icao == "KTST");
    CHECK(event->runway_id == "27");
    CHECK(event->category == SightingCategory::kArrival);
    CHECK(event->time_sec == 30.0);
}

TEST_CASE("SightingTracker: ProcessSlot carries the confirming observation's callsign onto the RunwayEvent",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    SightingTracker::SlotObservation ground{"KTST", "09", "27", FlightPhase::kTaxi};
    tracker.ProcessSlot(1, slot, ground, 0.0);

    SightingTracker::SlotObservation climb{"KTST", "09", "27", FlightPhase::kInitialClimb};
    climb.callsign = "DLH56C";
    const auto event = tracker.ProcessSlot(1, slot, climb, 10.0);

    REQUIRE(event.has_value());
    CHECK(event->callsign == "DLH56C");
}

TEST_CASE("SightingTracker: single-runway airport auto-activates the other category on the same runway_id",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    SightingTracker::SlotObservation ground{"KTST", "09", "27", FlightPhase::kTaxi};
    ground.single_runway_airport = true;
    tracker.ProcessSlot(1, slot, ground, 0.0);

    SightingTracker::SlotObservation climb{"KTST", "09", "27", FlightPhase::kInitialClimb};
    climb.single_runway_airport = true;
    const auto event = tracker.ProcessSlot(1, slot, climb, 10.0);

    REQUIRE(event.has_value());
    CHECK(event->category == SightingCategory::kDeparture);
    // The confirmed departure on 09 also activates 09 for arrivals -- there
    // is no other pavement a landing at this airport could use.
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "09") == 1);
    // The reciprocal end must still not be activated by this.
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "27") == 0);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "27") == 0);
}

TEST_CASE("SightingTracker: single-runway auto-activation does not fire for a multi-runway airport",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    // single_runway_airport left at its default (false) -- same sequence as
    // the "real departure sequence records exactly once" test above.
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 10.0);

    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "09") == 0);
}

TEST_CASE("SightingTracker: single-runway auto-activation on a confirmed arrival activates departures too",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;

    SightingTracker::SlotObservation approach1{"KTST", "27", "09", FlightPhase::kFinalApproach};
    approach1.single_runway_airport = true;
    tracker.ProcessSlot(1, slot, approach1, 0.0);
    tracker.ProcessSlot(1, slot, approach1, 10.0);
    tracker.ProcessSlot(1, slot, approach1, 20.0);

    SightingTracker::SlotObservation rollout{"KTST", "27", "09", FlightPhase::kLandingRollout};
    rollout.single_runway_airport = true;
    const auto event = tracker.ProcessSlot(1, slot, rollout, 30.0);

    REQUIRE(event.has_value());
    CHECK(event->category == SightingCategory::kArrival);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kArrival, "27") == 1);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "27") == 1);
}

TEST_CASE("SightingTracker: PruneStaleSightings drops old contributors and empty runway entries",
          "[SightingTracker]")
{
    SightingTracker tracker;
    SlotSightingState slot;
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, slot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 10.0);
    REQUIRE(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1);

    tracker.PruneStaleSightings(/*nowSec=*/5000.0, /*maxAgeSec=*/5400.0);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 1); // still fresh enough

    tracker.PruneStaleSightings(/*nowSec=*/6000.0, /*maxAgeSec=*/5400.0);
    CHECK(ContributorCount(tracker, "KTST", SightingCategory::kDeparture, "09") == 0); // now stale
}
