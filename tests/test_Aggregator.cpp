#include <catch2/catch_test_macros.hpp>

#include "core/Aggregator.h"

using namespace trm::core;

namespace {

// Two independent runway pairs so a departure on one pair can't reciprocally
// invalidate an arrival recorded on the other.
Airport MakeFourEndedAirport()
{
    Airport airport;
    airport.icao = "KTST";
    airport.runways.push_back(RunwayEnd{"09", 0.0, 0.0, 90.0, 45.0, "27", 5000.0});
    airport.runways.push_back(RunwayEnd{"27", 0.0, 0.0, 270.0, 45.0, "09", 5000.0});
    airport.runways.push_back(RunwayEnd{"18", 0.0, 0.0, 180.0, 45.0, "36", 6000.0});
    airport.runways.push_back(RunwayEnd{"36", 0.0, 0.0, 0.0, 45.0, "18", 6000.0});
    return airport;
}

Airport MakeSingleRunwayAirport()
{
    Airport airport;
    airport.icao = "KTST";
    airport.runways.push_back(RunwayEnd{"09", 0.0, 0.0, 90.0, 45.0, "27", 5000.0});
    airport.runways.push_back(RunwayEnd{"27", 0.0, 0.0, 270.0, 45.0, "09", 5000.0});
    return airport;
}

} // namespace

TEST_CASE("RunwaysWithSightings: counts distinct contributors within the window", "[Aggregator]")
{
    RunwaySightings sightings;
    sightings["09"][1] = 100.0;
    sightings["09"][2] = 150.0; // most recent
    sightings["27"][3] = 0.0;   // outside the window below

    const double nowSec = 200.0;
    const double windowSec = 100.0; // only sightings within [100, 200] count

    const auto results = RunwaysWithSightings(&sightings, windowSec, nowSec, nullptr);

    REQUIRE(results.size() == 1); // "27" excluded entirely: its only contributor is 200s old
    CHECK(results[0].runway_id == "09");
    CHECK(results[0].count == 2);
    CHECK(results[0].elapsed_sec == 50.0);
}

TEST_CASE("RunwaysWithSightings: attaches length_ft from the airport when supplied", "[Aggregator]")
{
    Airport airport;
    airport.runways.push_back(RunwayEnd{"09", 0.0, 0.0, 90.0, 45.0, "27", 7000.0});
    RunwaySightings sightings;
    sightings["09"][1] = 0.0;

    const auto results = RunwaysWithSightings(&sightings, 100.0, 0.0, &airport);
    REQUIRE(results.size() == 1);
    REQUIRE(results[0].length_ft.has_value());
    CHECK(*results[0].length_ft == 7000.0);
}

TEST_CASE("RunwaysWithSightings: length_ft is nullopt without an airport, and nullptr sightings yields empty",
          "[Aggregator]")
{
    RunwaySightings sightings;
    sightings["09"][1] = 0.0;
    const auto results = RunwaysWithSightings(&sightings, 100.0, 0.0, nullptr);
    REQUIRE(results.size() == 1);
    CHECK_FALSE(results[0].length_ft.has_value());

    CHECK(RunwaysWithSightings(nullptr, 100.0, 0.0, nullptr).empty());
}

TEST_CASE("BuildCategoryResult: non-empty active suppresses history", "[Aggregator]")
{
    RunwaySightings sightings;
    sightings["09"][1] = 90.0; // within the active window below

    const auto result = BuildCategoryResult(&sightings, /*activeWindowSec=*/1800.0, /*historyWindowSec=*/5400.0,
                                             /*nowSec=*/100.0, nullptr);

    REQUIRE(result.active.size() == 1);
    CHECK(result.active[0].runway_id == "09");
    CHECK_FALSE(result.history.has_value());
    CHECK_FALSE(result.NeedsEstimate());
}

TEST_CASE("BuildCategoryResult: history picks by recency, not count, when active is empty", "[Aggregator]")
{
    const double nowSec = 6000.0;
    // Both outside the 1800s active window, both inside the 5400s history
    // window. "09" has more contributors (count 2) but "27"'s single
    // contributor is more recent.
    RunwaySightings sightings;
    sightings["09"][1] = 1000.0; // elapsed 5000
    sightings["09"][2] = 1200.0; // elapsed 4800 (09's most recent)
    sightings["27"][3] = 3000.0; // elapsed 3000 (more recent than 09's)

    const auto result = BuildCategoryResult(&sightings, /*activeWindowSec=*/1800.0, /*historyWindowSec=*/5400.0,
                                             nowSec, nullptr);

    REQUIRE(result.active.empty());
    REQUIRE(result.history.has_value());
    CHECK(result.history->runway_id == "27"); // recency wins despite lower count
    CHECK_FALSE(result.NeedsEstimate());  // history present -> doesn't need a wind estimate
}

TEST_CASE("BuildCategoryResult: no data at all needs a wind estimate", "[Aggregator]")
{
    RunwaySightings empty;
    const auto result = BuildCategoryResult(&empty, 1800.0, 5400.0, 1000.0, nullptr);
    CHECK(result.active.empty());
    CHECK_FALSE(result.history.has_value());
    CHECK(result.NeedsEstimate());
}

TEST_CASE("BuildAirportEntry: wind estimate only appears when a category has neither active nor history data",
          "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFourEndedAirport();
    AirportEntryInputs inputs;
    inputs.wind_airport_position_reading = WindReading{10.0, 90.0, true};

    SECTION("no sightings at all -> wind estimate present") {
        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.arrivals.active.empty());
        REQUIRE_FALSE(entry.arrivals.history.has_value());
        REQUIRE(entry.arrivals_estimate.has_value());
        CHECK(entry.arrivals_estimate->runway_id == "09");
    }

    SECTION("real active traffic on both categories -> no wind estimate") {
        SlotSightingState depSlot;
        SlotSightingState arrSlot;
        // Departure on 09 (independent of the 18/36 pair used for the arrival below).
        tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
        tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 30.0);
        // Arrival on 18 (independent runway pair -- doesn't reciprocally invalidate 09).
        // Three final_approach cycles to clear SightingConfig::final_approach_confirm_cycles (default 3).
        tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kFinalApproach}, 0.0);
        tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kFinalApproach}, 10.0);
        tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kFinalApproach}, 20.0);
        tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kLandingRollout}, 30.0);

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.departures.active.size() == 1);
        REQUIRE(entry.arrivals.active.size() == 1);
        CHECK_FALSE(entry.arrivals_estimate.has_value());
    }
}

TEST_CASE("BuildAirportEntry: single-runway airport infers the other category active, with a real (zero) count",
          "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeSingleRunwayAirport();
    SlotSightingState depSlot;

    // Confirmed departure on 09 -- no arrival ever recorded.
    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 30.0);

    AirportEntryInputs inputs;
    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 100.0);

    REQUIRE(entry.departures.active.size() == 1);
    CHECK(entry.departures.active[0].runway_id == "09");
    CHECK(entry.departures.active[0].count == 1);
    CHECK_FALSE(entry.departures.active[0].inferred);

    // Arrivals show 09 as active too (single runway, no other pavement),
    // but the count stays a true 0 -- nothing has actually been observed
    // landing here.
    REQUIRE(entry.arrivals.active.size() == 1);
    CHECK(entry.arrivals.active[0].runway_id == "09");
    CHECK(entry.arrivals.active[0].count == 0);
    CHECK(entry.arrivals.active[0].inferred);
    CHECK_FALSE(entry.arrivals.history.has_value());
}

TEST_CASE("BuildAirportEntry: single-runway inference does not fire for a multi-runway airport", "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFourEndedAirport();
    SlotSightingState depSlot;

    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 30.0);

    AirportEntryInputs inputs;
    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 100.0);

    REQUIRE(entry.departures.active.size() == 1);
    CHECK(entry.arrivals.active.empty());
}

TEST_CASE("BuildAirportEntry: single-runway inference is a no-op once the other category has its own real traffic",
          "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeSingleRunwayAirport();
    SlotSightingState depSlot;
    SlotSightingState arrSlot;

    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 30.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "09", "27", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "09", "27", FlightPhase::kFinalApproach}, 10.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "09", "27", FlightPhase::kFinalApproach}, 20.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "09", "27", FlightPhase::kLandingRollout}, 30.0);

    AirportEntryInputs inputs;
    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 100.0);

    REQUIRE(entry.arrivals.active.size() == 1);
    CHECK(entry.arrivals.active[0].count == 1);
    CHECK_FALSE(entry.arrivals.active[0].inferred);
    REQUIRE(entry.departures.active.size() == 1);
    CHECK(entry.departures.active[0].count == 1);
    CHECK_FALSE(entry.departures.active[0].inferred);
}

TEST_CASE("BuildAirportEntry: name is populated from the airport, nullopt without one", "[Aggregator]")
{
    SightingTracker tracker;
    AirportEntryInputs inputs;

    SECTION("named airport") {
        Airport airport = MakeFourEndedAirport();
        airport.name = "Test Airport";
        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.name.has_value());
        CHECK(*entry.name == "Test Airport");
    }
    SECTION("airport with empty name") {
        Airport airport = MakeFourEndedAirport();
        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        CHECK_FALSE(entry.name.has_value());
    }
    SECTION("no airport at all") {
        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, nullptr, tracker, inputs, 1000.0);
        CHECK_FALSE(entry.name.has_value());
    }
}

TEST_CASE("BuildAirportEntry: current_wind is populated even when both categories have active traffic",
          "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFourEndedAirport();

    SlotSightingState depSlot;
    SlotSightingState arrSlot;
    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, depSlot, {"KTST", "09", "27", FlightPhase::kInitialClimb}, 30.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kFinalApproach}, 0.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kFinalApproach}, 10.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kFinalApproach}, 20.0);
    tracker.ProcessSlot(2, arrSlot, {"KTST", "18", "36", FlightPhase::kLandingRollout}, 30.0);

    AirportEntryInputs inputs;
    inputs.wind_airport_position_reading = WindReading{12.0, 270.0, true};

    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
    REQUIRE(entry.departures.active.size() == 1);
    REQUIRE(entry.arrivals.active.size() == 1);
    CHECK_FALSE(entry.arrivals_estimate.has_value()); // neither category needs a fallback guess

    REQUIRE(entry.current_wind.has_value());
    CHECK(entry.current_wind->speed_kt == 12.0);
    CHECK(entry.current_wind->direction_true_deg == 270.0);
}

TEST_CASE("BuildAirportEntry: current_wind is nullopt with no wind inputs at all", "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFourEndedAirport();
    AirportEntryInputs inputs;

    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
    CHECK_FALSE(entry.current_wind.has_value());
}

TEST_CASE("BuildAirportEntry: altimeter_pa comes from the airport-position reading only", "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFourEndedAirport();

    SECTION("airport-position reading present -> altimeter_pa set") {
        AirportEntryInputs inputs;
        inputs.wind_airport_position_reading = WindReading{10.0, 90.0, true, 101325.0};

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.altimeter_pa.has_value());
        CHECK(*entry.altimeter_pa == 101325.0);
    }

    SECTION("only aircraft-position/region reading -> altimeter_pa stays nullopt") {
        AirportEntryInputs inputs;
        inputs.wind_aircraft_position_reading = WindReading{10.0, 90.0, false};

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        CHECK_FALSE(entry.altimeter_pa.has_value());
    }

    SECTION("no wind inputs at all -> altimeter_pa stays nullopt") {
        AirportEntryInputs inputs;

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        CHECK_FALSE(entry.altimeter_pa.has_value());
    }
}

TEST_CASE("BuildAirportEntry: current_wind source upgrades to own_station when METAR is available, same as "
          "wind_estimate",
          "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFourEndedAirport();

    AirportEntryInputs inputs;
    inputs.wind_airport_position_reading = WindReading{10.0, 90.0, true};
    inputs.metar = "KTST 151200Z 09010KT 10SM CLR 22/10 A3000";

    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
    REQUIRE(entry.current_wind.has_value());
    CHECK(entry.current_wind->source == WindEstimateSource::kOwnStation);
}

TEST_CASE("BuildAirportEntry: upgrades wind_estimate source to own_station when METAR is available", "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFourEndedAirport();

    AirportEntryInputs inputs;
    inputs.wind_airport_position_reading = WindReading{10.0, 90.0, true}; // has_station_match -> station initially
    inputs.metar = "KTST 151200Z 09010KT 10SM CLR 22/10 A3000";

    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);

    REQUIRE(entry.arrivals_estimate.has_value());
    CHECK(entry.arrivals_estimate->source == WindEstimateSource::kOwnStation);
}

TEST_CASE("SelectPinnedAirport: origin/destination switchover at the exact radius boundary", "[Aggregator]")
{
    const std::string origin = "KORG";
    const std::string destination = "KDST";
    const double radius = 10.0;

    SECTION("exactly at radius -> origin") {
        const auto sel = SelectPinnedAirport(origin, destination, 10.0, radius);
        REQUIRE(sel.has_value());
        CHECK(sel->kind == PinnedKind::kOrigin);
    }
    SECTION("just inside radius -> origin") {
        const auto sel = SelectPinnedAirport(origin, destination, 9.99, radius);
        REQUIRE(sel.has_value());
        CHECK(sel->kind == PinnedKind::kOrigin);
    }
    SECTION("just outside radius -> destination") {
        const auto sel = SelectPinnedAirport(origin, destination, 10.01, radius);
        REQUIRE(sel.has_value());
        CHECK(sel->kind == PinnedKind::kDestination);
    }
    SECTION("origin distance unknown -> destination") {
        const auto sel = SelectPinnedAirport(origin, destination, std::nullopt, radius);
        REQUIRE(sel.has_value());
        CHECK(sel->kind == PinnedKind::kDestination);
    }
    SECTION("only origin known -> origin") {
        const auto sel = SelectPinnedAirport(origin, std::nullopt, std::nullopt, radius);
        REQUIRE(sel.has_value());
        CHECK(sel->icao == origin);
        CHECK(sel->kind == PinnedKind::kOrigin);
    }
    SECTION("only destination known -> destination") {
        const auto sel = SelectPinnedAirport(std::nullopt, destination, std::nullopt, radius);
        REQUIRE(sel.has_value());
        CHECK(sel->icao == destination);
        CHECK(sel->kind == PinnedKind::kDestination);
    }
    SECTION("neither known -> nullopt") {
        CHECK_FALSE(SelectPinnedAirport(std::nullopt, std::nullopt, std::nullopt, radius).has_value());
    }
}

TEST_CASE("BuildNearbyCandidates: excludes the pinned airport and caps at maxDisplayed", "[Aggregator]")
{
    const std::vector<NearbyAirport> nearest = {
        {"KAAA", "Airport AAA", 1.0}, {"KBBB", "Airport BBB", 2.0}, {"KCCC", "Airport CCC", 3.0},
        {"KDDD", "Airport DDD", 4.0}, {"KEEE", "Airport EEE", 5.0}, {"KFFF", "Airport FFF", 6.0},
    };

    SECTION("excludes the pinned airport") {
        const auto candidates = BuildNearbyCandidates(nearest, std::string("KBBB"), 10);
        REQUIRE(candidates.size() == 5);
        for (const auto& c : candidates) {
            CHECK(c.icao != "KBBB");
        }
    }
    SECTION("caps at maxDisplayed") {
        const auto candidates = BuildNearbyCandidates(nearest, std::nullopt, 3);
        REQUIRE(candidates.size() == 3);
        CHECK(candidates[0].icao == "KAAA");
        CHECK(candidates[2].icao == "KCCC");
    }
    SECTION("carries the airport name through") {
        const auto candidates = BuildNearbyCandidates(nearest, std::nullopt, 1);
        REQUIRE(candidates.size() == 1);
        CHECK(candidates[0].name == "Airport AAA");
    }
}

// --- flow tier (apt.dat ATC flows) --------------------------------------

namespace {

// Two parallels plus an authored flow that lands one and departs the other,
// the EGLL/LEPA shape. Wind rules mirror LEPA's: a calm clause first, then a
// directional one.
Airport MakeFlowAirport()
{
    Airport airport;
    airport.icao = "KTST";
    airport.runways.push_back(RunwayEnd{"09L", 0.0, 0.0, 90.0, 45.0, "27R", 9000.0});
    airport.runways.push_back(RunwayEnd{"27R", 0.0, 0.1, 270.0, 45.0, "09L", 9000.0});
    airport.runways.push_back(RunwayEnd{"09R", -0.01, 0.0, 90.0, 45.0, "27L", 9000.0});
    airport.runways.push_back(RunwayEnd{"27L", -0.01, 0.1, 270.0, 45.0, "09R", 9000.0});

    TrafficFlow west;
    west.name = "West Flow";
    west.wind_rules.push_back(FlowWindRule{0.0, 360.0, 10.0});
    west.wind_rules.push_back(FlowWindRule{150.0, 330.0, 999.0});
    west.runway_use_rules.push_back(FlowRunwayUseRule{"27R", true, false, {"jets"}});
    west.runway_use_rules.push_back(FlowRunwayUseRule{"27L", false, true, {"jets"}});
    airport.flows.push_back(std::move(west));

    TrafficFlow east; // unconditional fallback, no wind rules
    east.name = "East Flow";
    east.runway_use_rules.push_back(FlowRunwayUseRule{"09L", true, false, {"jets"}});
    east.runway_use_rules.push_back(FlowRunwayUseRule{"09R", false, true, {"jets"}});
    airport.flows.push_back(std::move(east));

    return airport;
}

} // namespace

TEST_CASE("BuildAirportEntry: an authored flow gives arrivals and departures different runways",
          "[Aggregator]")
{
    SightingTracker tracker;
    const Airport airport = MakeFlowAirport();

    AirportEntryInputs inputs;
    inputs.wind_airport_position_reading = WindReading{15.0, 270.0, true};

    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);

    REQUIRE(entry.arrivals_estimate.has_value());
    REQUIRE(entry.departures_estimate.has_value());
    CHECK(entry.arrivals_estimate->runway_id == "27R");
    CHECK(entry.departures_estimate->runway_id == "27L");
    CHECK(entry.arrivals_estimate->rule_source == ActiveRunwaySource::kSimFlow);
    CHECK(entry.arrivals_estimate->flow_name == "West Flow");
}

TEST_CASE("BuildAirportEntry regression: a dead calm still consults flows, and only the crosswind "
          "tier treats it as favoring nothing",
          "[Aggregator]")
{
    SightingTracker tracker;

    SECTION("airport with flows: the leading calm-wind clause still answers") {
        const Airport airport = MakeFlowAirport();
        AirportEntryInputs inputs;
        inputs.wind_airport_position_reading = WindReading{0.0, 0.0, true};

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);

        REQUIRE(entry.arrivals_estimate.has_value());
        CHECK(entry.arrivals_estimate->runway_id == "27R");
        CHECK(entry.arrivals_estimate->flow_name == "West Flow");
    }
    SECTION("airport without flows: dead calm favors nothing, as before") {
        const Airport airport = MakeFourEndedAirport();
        AirportEntryInputs inputs;
        inputs.wind_airport_position_reading = WindReading{0.0, 0.0, true};

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);

        CHECK_FALSE(entry.arrivals_estimate.has_value());
        CHECK_FALSE(entry.departures_estimate.has_value());
    }
}

TEST_CASE("BuildAirportEntry: ceiling and visibility from the airport-position reading gate a flow",
          "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFlowAirport();
    airport.flows[0].min_ceiling_ft = 200.0;
    airport.flows[0].min_visibility_sm = 0.5;

    SECTION("good weather -> the gated flow still wins") {
        AirportEntryInputs inputs;
        WindReading reading{15.0, 270.0, true};
        reading.ceiling_ft = 3000.0;
        reading.visibility_sm = 10.0;
        inputs.wind_airport_position_reading = reading;

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.arrivals_estimate.has_value());
        CHECK(entry.arrivals_estimate->flow_name == "West Flow");
    }
    SECTION("below minimums -> falls through to the unconditional flow") {
        AirportEntryInputs inputs;
        WindReading reading{15.0, 270.0, true};
        reading.ceiling_ft = 100.0;
        reading.visibility_sm = 0.25;
        inputs.wind_airport_position_reading = reading;

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.arrivals_estimate.has_value());
        CHECK(entry.arrivals_estimate->flow_name == "East Flow");
        CHECK(entry.arrivals_estimate->runway_id == "09L");
    }
    SECTION("an unmeasured reading leaves the gated flow eligible") {
        AirportEntryInputs inputs;
        inputs.wind_airport_position_reading = WindReading{15.0, 270.0, true}; // ceiling/vis at defaults

        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.arrivals_estimate.has_value());
        CHECK(entry.arrivals_estimate->flow_name == "West Flow");
    }
}

TEST_CASE("BuildAirportEntry: a category with confirmed traffic gets no estimate, while the other still does",
          "[Aggregator]")
{
    SightingTracker tracker;
    const Airport airport = MakeFlowAirport();

    // A confirmed departure on 09R only.
    SlotSightingState depSlot;
    tracker.ProcessSlot(1, depSlot, {"KTST", "09R", "27L", FlightPhase::kTaxi}, 0.0);
    tracker.ProcessSlot(1, depSlot, {"KTST", "09R", "27L", FlightPhase::kInitialClimb}, 30.0);

    AirportEntryInputs inputs;
    inputs.wind_airport_position_reading = WindReading{15.0, 270.0, true};

    const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);

    REQUIRE(entry.departures.active.size() == 1);
    CHECK_FALSE(entry.departures_estimate.has_value()); // real traffic outranks the flow rule
    REQUIRE(entry.arrivals_estimate.has_value());
    CHECK(entry.arrivals_estimate->runway_id == "27R");
}

TEST_CASE("BuildAirportEntry: a flow time rule is evaluated against the supplied zulu minutes", "[Aggregator]")
{
    SightingTracker tracker;
    Airport airport = MakeFlowAirport();
    airport.flows[0].time_rule = FlowTimeRule{7 * 60, 23 * 60}; // day only

    AirportEntryInputs inputs;
    inputs.wind_airport_position_reading = WindReading{15.0, 270.0, true};

    SECTION("inside the window -> day flow") {
        inputs.utc_minutes = 12 * 60;
        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.arrivals_estimate.has_value());
        CHECK(entry.arrivals_estimate->flow_name == "West Flow");
    }
    SECTION("outside it -> falls through") {
        inputs.utc_minutes = 2 * 60;
        const AirportEntry entry = BuildAirportEntry("KTST", std::nullopt, &airport, tracker, inputs, 1000.0);
        REQUIRE(entry.arrivals_estimate.has_value());
        CHECK(entry.arrivals_estimate->flow_name == "East Flow");
    }
}
