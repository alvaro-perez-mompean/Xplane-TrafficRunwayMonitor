#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "core/ActiveRunway.h"

using namespace trm::core;

namespace {

// Two parallel east/west runways, 09L/27R and 09R/27L, so both the
// arrivals-vs-departures split and the parallel-tie-break cases are reachable.
Airport MakeParallelAirport()
{
    Airport airport;
    airport.icao = "KTST";
    airport.runways.push_back(RunwayEnd{"09L", 0.0, 0.0, 90.0, 45.0, "27R", 9000.0});
    airport.runways.push_back(RunwayEnd{"27R", 0.0, 0.1, 270.0, 45.0, "09L", 9000.0});
    airport.runways.push_back(RunwayEnd{"09R", -0.01, 0.0, 90.0, 45.0, "27L", 9000.0});
    airport.runways.push_back(RunwayEnd{"27L", -0.01, 0.1, 270.0, 45.0, "09R", 9000.0});
    return airport;
}

ActiveRunwayConditions Wind(double fromDeg, double speedKt)
{
    ActiveRunwayConditions conditions;
    conditions.wind_from_true_deg = fromDeg;
    conditions.wind_speed_kt = speedKt;
    return conditions;
}

Airport ParseSingleAirport(const std::string& aptDat)
{
    std::istringstream in(aptDat);
    const AirportDatabase db = ParseAptDat(in);
    REQUIRE(db.count("KTST") == 1);
    return db.at("KTST");
}

const char* kTwoRunwayHeader =
    "1 13 0 0 KTST Test Airport\n"
    "100 45.00 1 0 0 0 0 0 09L 0.0000 0.0000 0 0 0 0 0 0 27R 0.0000 0.1000 0 0 0 0 0 0\n"
    "100 45.00 1 0 0 0 0 0 09R -0.0100 0.0000 0 0 0 0 0 0 27L -0.0100 0.1000 0 0 0 0 0 0\n";

} // namespace

// --- Tier 1: flow replication -------------------------------------------

TEST_CASE("SelectActiveRunway: a matched flow splits arrivals and departures onto different runways",
          "[ActiveRunway]")
{
    // The LEPA shape: arrivals on one parallel, departures on the other. This
    // is the case the wind tier structurally cannot express, since it produces
    // one runway for both categories.
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 Westerly Flow\n"
                                                "1001 KTST 180 359 999\n"
                                                "1110 27R 118955 arrivals heavy|jets|props 000000 000000\n"
                                                "1110 27L 118955 departures heavy|jets|props 000000 000000\n"
                                                "99\n");

    const auto arrival = SelectActiveRunway(airport, Wind(270.0, 12.0), RunwayOperation::kArrival);
    const auto departure = SelectActiveRunway(airport, Wind(270.0, 12.0), RunwayOperation::kDeparture);

    REQUIRE(arrival.has_value());
    REQUIRE(departure.has_value());
    CHECK(arrival->runway_id == "27R");
    CHECK(departure->runway_id == "27L");
    CHECK(arrival->source == ActiveRunwaySource::kSimFlow);
    CHECK(arrival->flow_name == "Westerly Flow");
    CHECK(departure->flow_name == "Westerly Flow");
}

TEST_CASE("SelectActiveRunway: the FIRST flow whose rules pass wins, not the best-matching one",
          "[ActiveRunway]")
{
    // Both flows' wind sectors admit a 270 wind. File order decides.
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 First\n"
                                                "1001 KTST 180 359 999\n"
                                                "1110 27R 118955 arrivals|departures jets 000000 000000\n"
                                                "1000 Second\n"
                                                "1001 KTST 260 280 999\n"
                                                "1110 27L 118955 arrivals|departures jets 000000 000000\n"
                                                "99\n");

    const auto result = SelectActiveRunway(airport, Wind(270.0, 10.0), RunwayOperation::kArrival);
    REQUIRE(result.has_value());
    CHECK(result->runway_id == "27R");
    CHECK(result->flow_name == "First");
}

TEST_CASE("SelectActiveRunway regression: multiple wind rules in one flow are OR'd, so a leading "
          "calm-wind clause is not lost",
          "[ActiveRunway]")
{
    // The real LEPA "ATC West Flow" shape: a calm clause first, a directional
    // clause second, then an unconditional fallback flow. Keeping only the last
    // wind rule parsed would make the calm case fall through to East.
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 ATC West Flow\n"
                                                "1001 KTST 000 360 10\n"
                                                "1001 KTST 150 330 999\n"
                                                "1110 27R 118955 arrivals|departures jets 000000 000000\n"
                                                "1000 ATC East Flow\n"
                                                "1110 09L 118955 arrivals|departures jets 000000 000000\n"
                                                "99\n");

    REQUIRE(airport.flows.size() == 2);
    REQUIRE(airport.flows[0].wind_rules.size() == 2);

    SECTION("dead calm matches the first (calm) clause, not the fallback flow") {
        const auto result = SelectActiveRunway(airport, Wind(0.0, 0.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "27R");
        CHECK(result->flow_name == "ATC West Flow");
    }
    SECTION("a westerly wind matches the second (directional) clause") {
        const auto result = SelectActiveRunway(airport, Wind(270.0, 20.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "27R");
    }
    SECTION("an easterly wind above the calm limit matches neither clause, so the fallback flow wins") {
        const auto result = SelectActiveRunway(airport, Wind(090.0, 20.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
        CHECK(result->flow_name == "ATC East Flow");
    }
}

TEST_CASE("SelectActiveRunway: a wind sector wrapping through 360/0 is matched correctly", "[ActiveRunway]")
{
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 East bound\n"
                                                "1001 KTST 348 167 999\n"
                                                "1110 09L 118955 arrivals|departures jets 000000 000000\n"
                                                "1000 West bound\n"
                                                "1001 KTST 168 347 999\n"
                                                "1110 27R 118955 arrivals|departures jets 000000 000000\n"
                                                "99\n");

    SECTION("just inside the wrapping sector, above 360") {
        const auto result = SelectActiveRunway(airport, Wind(350.0, 10.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
    SECTION("just inside the wrapping sector, below 360") {
        const auto result = SelectActiveRunway(airport, Wind(10.0, 10.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
    SECTION("outside it") {
        const auto result = SelectActiveRunway(airport, Wind(200.0, 10.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "27R");
    }
}

TEST_CASE("SelectActiveRunway: a flow's max wind speed gates it", "[ActiveRunway]")
{
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 Light wind only\n"
                                                "1001 KTST 000 360 15\n"
                                                "1110 09L 118955 arrivals|departures jets 000000 000000\n"
                                                "1000 Anything\n"
                                                "1110 27R 118955 arrivals|departures jets 000000 000000\n"
                                                "99\n");

    SECTION("at the limit -> first flow still applies") {
        const auto result = SelectActiveRunway(airport, Wind(90.0, 15.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
    SECTION("above the limit -> falls through") {
        const auto result = SelectActiveRunway(airport, Wind(90.0, 15.1), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "27R");
    }
}

TEST_CASE("SelectActiveRunway: ceiling and visibility minima gate a flow, and 0 means unrestricted",
          "[ActiveRunway]")
{
    // The KJFK shape: a CAT I flow with real minima ahead of a low-visibility
    // catch-all whose minima are the inert 0/0.0.
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 CAT I\n"
                                                "1002 KTST 200\n"
                                                "1003 KTST 0.5\n"
                                                "1110 09L 118955 arrivals|departures jets 000000 000000\n"
                                                "1000 LOW VIS\n"
                                                "1002 KTST 0\n"
                                                "1003 KTST 0.0\n"
                                                "1110 27R 118955 arrivals|departures jets 000000 000000\n"
                                                "99\n");

    SECTION("good weather -> CAT I flow") {
        ActiveRunwayConditions conditions = Wind(90.0, 10.0);
        conditions.ceiling_ft = 3000.0;
        conditions.visibility_sm = 10.0;
        const auto result = SelectActiveRunway(airport, conditions, RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
    SECTION("ceiling below minimum -> low-vis flow") {
        ActiveRunwayConditions conditions = Wind(90.0, 10.0);
        conditions.ceiling_ft = 100.0;
        conditions.visibility_sm = 10.0;
        const auto result = SelectActiveRunway(airport, conditions, RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "27R");
    }
    SECTION("visibility below minimum -> low-vis flow") {
        ActiveRunwayConditions conditions = Wind(90.0, 10.0);
        conditions.ceiling_ft = 3000.0;
        conditions.visibility_sm = 0.25;
        const auto result = SelectActiveRunway(airport, conditions, RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "27R");
    }
    SECTION("unmeasured weather defaults to unrestricted, so the CAT I flow is still eligible") {
        const auto result = SelectActiveRunway(airport, Wind(90.0, 10.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
}

TEST_CASE("SelectActiveRunway: a time rule gates a flow, including a window wrapping midnight",
          "[ActiveRunway]")
{
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 Night\n"
                                                "1004 2000 0500\n"
                                                "1110 09L 118955 arrivals|departures jets 000000 000000\n"
                                                "1000 Day\n"
                                                "1110 27R 118955 arrivals|departures jets 000000 000000\n"
                                                "99\n");

    auto at = [&](int utcMinutes) {
        ActiveRunwayConditions conditions = Wind(90.0, 10.0);
        conditions.utc_minutes = utcMinutes;
        return SelectActiveRunway(airport, conditions, RunwayOperation::kArrival);
    };

    SECTION("late evening, inside the wrapping window") {
        const auto result = at(22 * 60);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
    SECTION("early morning, still inside it") {
        const auto result = at(2 * 60);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
    SECTION("midday, outside it") {
        const auto result = at(12 * 60);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "27R");
    }
}

TEST_CASE("SelectActiveRunway: aircraft class filters runway-in-use rules, and an empty class matches any",
          "[ActiveRunway]")
{
    // The KSEA shape: heavies and light traffic split across parallels.
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 Only flow\n"
                                                "1110 09L 118955 arrivals props|helos 000000 000000\n"
                                                "1110 09R 118955 arrivals heavy|jets 000000 000000\n"
                                                "99\n");

    SECTION("a named class picks the rule listing it, even though it is not first") {
        ActiveRunwayConditions conditions = Wind(90.0, 10.0);
        conditions.aircraft_class = "heavy";
        const auto result = SelectActiveRunway(airport, conditions, RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09R");
    }
    SECTION("no class named -> first rule covering the operation, in file order") {
        const auto result = SelectActiveRunway(airport, Wind(90.0, 10.0), RunwayOperation::kArrival);
        REQUIRE(result.has_value());
        CHECK(result->runway_id == "09L");
    }
}

TEST_CASE("SelectActiveRunway: a matched flow with no rule covering this operation falls through to "
          "the wind tier, not to the next flow",
          "[ActiveRunway]")
{
    // The LILI / KMMH shape: a flow that matches on wind but covers only the
    // other operation. Advancing to the next flow would pick one whose
    // conditions do not hold, so the wind tier answers instead.
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 North\n"
                                                "1001 KTST 270 090 999\n"
                                                "1000 South\n"
                                                "1001 KTST 090 270 999\n"
                                                "1110 27R 118955 arrivals|departures jets 000000 000000\n"
                                                "99\n");

    const auto result = SelectActiveRunway(airport, Wind(360.0, 10.0), RunwayOperation::kArrival);
    REQUIRE(result.has_value());
    CHECK(result->source == ActiveRunwaySource::kCrosswind);
    CHECK(result->flow_name.empty());
}

TEST_CASE("SelectActiveRunway: a runway-in-use rule naming a runway the airport does not have is skipped",
          "[ActiveRunway]")
{
    const Airport airport = ParseSingleAirport(std::string(kTwoRunwayHeader) +
                                                "1000 Only flow\n"
                                                "1110 18C 118955 arrivals jets 000000 000000\n"
                                                "1110 09L 118955 arrivals jets 000000 000000\n"
                                                "99\n");

    const auto result = SelectActiveRunway(airport, Wind(90.0, 10.0), RunwayOperation::kArrival);
    REQUIRE(result.has_value());
    CHECK(result->runway_id == "09L");
    CHECK(result->source == ActiveRunwaySource::kSimFlow);
}

TEST_CASE("SelectActiveRunway regression: a flow rule's runway id matches the airport's own spelling "
          "even when they disagree about leading zeros",
          "[ActiveRunway]")
{
    // The real KJFK case: row 100 spells the ends "4L"/"4R", the flow rules
    // spell them "04L"/"04R". 837 of 7,256 runway-in-use rows in the shipped
    // apt.dat have this mismatch, so comparing literally loses flow selection
    // at a long list of major airports.
    const Airport airport = ParseSingleAirport(
        "1 13 0 0 KTST Test Airport\n"
        "100 45.00 1 0 0 0 0 0 4L 0.0000 0.0000 0 0 0 0 0 0 22R 0.0000 0.1000 0 0 0 0 0 0\n"
        "100 45.00 1 0 0 0 0 0 4R -0.0100 0.0000 0 0 0 0 0 0 22L -0.0100 0.1000 0 0 0 0 0 0\n"
        "1000 04R LOW VIS\n"
        "1110 04R 135900 arrivals heavy|jets 000359 050090\n"
        "1110 04L 135900 departures heavy|jets 000359 010030\n"
        "99\n");

    const auto arrival = SelectActiveRunway(airport, Wind(40.0, 10.0), RunwayOperation::kArrival);
    const auto departure = SelectActiveRunway(airport, Wind(40.0, 10.0), RunwayOperation::kDeparture);

    REQUIRE(arrival.has_value());
    REQUIRE(departure.has_value());
    CHECK(arrival->source == ActiveRunwaySource::kSimFlow);
    // Reported using the airport's own spelling, not the rule's: everything
    // downstream is keyed on what row 100 called it.
    CHECK(arrival->runway_id == "4R");
    CHECK(departure->runway_id == "4L");
}

TEST_CASE("SelectActiveRunway: an airport with no flows falls straight to the wind tier", "[ActiveRunway]")
{
    const Airport airport = MakeParallelAirport();
    const auto result = SelectActiveRunway(airport, Wind(270.0, 12.0), RunwayOperation::kArrival);
    REQUIRE(result.has_value());
    CHECK(result->source == ActiveRunwaySource::kCrosswind);
}

TEST_CASE("SelectActiveRunway: an airport with no runways has no answer", "[ActiveRunway]")
{
    Airport airport;
    airport.icao = "KEMP";
    CHECK_FALSE(SelectActiveRunway(airport, Wind(270.0, 12.0), RunwayOperation::kArrival).has_value());
}

// --- Tier 2: crosswind fallback -----------------------------------------

TEST_CASE("SelectCrosswindFavoredRunway: picks the into-wind end", "[ActiveRunway]")
{
    const Airport airport = MakeParallelAirport();
    CHECK(SelectCrosswindFavoredRunway(airport, 90.0, 12.0) == "09L");
    CHECK(SelectCrosswindFavoredRunway(airport, 270.0, 12.0) == "27L");
}

TEST_CASE("SelectCrosswindFavoredRunway: rejects an end with more than the tolerated tailwind",
          "[ActiveRunway]")
{
    Airport airport;
    airport.icao = "KTST";
    // Two crossing runways: 09/27 and 13/31. A wind from 280 gives 27 a strong
    // headwind; 09 must be rejected on tailwind rather than merely outranked.
    airport.runways.push_back(RunwayEnd{"09", 0.0, 0.0, 90.0, 45.0, "27", 9000.0});
    airport.runways.push_back(RunwayEnd{"27", 0.0, 0.1, 270.0, 45.0, "09", 9000.0});

    ActiveRunwayConfig config;
    config.tailwind_tolerance_kt = 5.0;

    SECTION("a 20kt wind leaves only the into-wind end usable") {
        CHECK(SelectCrosswindFavoredRunway(airport, 280.0, 20.0, config) == "27");
    }
    SECTION("minimum crosswind wins over maximum headwind") {
        // Wind from 300: 27 has ~30 degrees of offset, 09 has ~150 (a tailwind,
        // rejected). Only 27 survives either way, which is the point.
        CHECK(SelectCrosswindFavoredRunway(airport, 300.0, 20.0, config) == "27");
    }
}

TEST_CASE("SelectCrosswindFavoredRunway regression: parallel ends with identical headings are broken "
          "by lowest runway id, not by apt.dat row order",
          "[ActiveRunway]")
{
    // 27L and 27R have exactly the same heading, so the old argmin-on-heading
    // approach returned whichever the parser happened to append first. Reversing
    // the parse order must not change the answer.
    Airport forward;
    forward.icao = "KTST";
    forward.runways.push_back(RunwayEnd{"27R", 0.0, 0.1, 270.0, 45.0, "09L", 9000.0});
    forward.runways.push_back(RunwayEnd{"27L", -0.01, 0.1, 270.0, 45.0, "09R", 9000.0});

    Airport reversed;
    reversed.icao = "KTST";
    reversed.runways.push_back(RunwayEnd{"27L", -0.01, 0.1, 270.0, 45.0, "09R", 9000.0});
    reversed.runways.push_back(RunwayEnd{"27R", 0.0, 0.1, 270.0, 45.0, "09L", 9000.0});

    CHECK(SelectCrosswindFavoredRunway(forward, 270.0, 15.0) == "27L");
    CHECK(SelectCrosswindFavoredRunway(reversed, 270.0, 15.0) == "27L");
}

TEST_CASE("SelectCrosswindFavoredRunway: below the calm threshold, direction is ignored entirely",
          "[ActiveRunway]")
{
    const Airport airport = MakeParallelAirport();
    ActiveRunwayConfig config;
    config.calm_wind_kt = 3.0;

    SECTION("calm: same deterministic answer regardless of direction") {
        const auto fromEast = SelectCrosswindFavoredRunway(airport, 90.0, 1.0, config);
        const auto fromWest = SelectCrosswindFavoredRunway(airport, 270.0, 1.0, config);
        REQUIRE(fromEast.has_value());
        CHECK(fromEast == fromWest);
        CHECK(*fromEast == "09L"); // lowest id
    }
    SECTION("at the threshold: direction matters again") {
        CHECK(SelectCrosswindFavoredRunway(airport, 270.0, 3.0, config) == "27L");
    }
}

TEST_CASE("SelectCrosswindFavoredRunway: an airport with no runways has no answer", "[ActiveRunway]")
{
    Airport airport;
    airport.icao = "KEMP";
    CHECK_FALSE(SelectCrosswindFavoredRunway(airport, 270.0, 12.0).has_value());
}

TEST_CASE("ActiveRunwaySourceLabel: every tier has a distinct, non-empty label", "[ActiveRunway]")
{
    const std::string flow = ActiveRunwaySourceLabel(ActiveRunwaySource::kSimFlow);
    const std::string crosswind = ActiveRunwaySourceLabel(ActiveRunwaySource::kCrosswind);
    CHECK_FALSE(flow.empty());
    CHECK_FALSE(crosswind.empty());
    CHECK(flow != crosswind);
}
