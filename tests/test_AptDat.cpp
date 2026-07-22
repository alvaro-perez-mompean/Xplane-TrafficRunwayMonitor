#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <sstream>

#include "core/AptDat.h"
#include "core/GeoMath.h"

using Catch::Approx;
using namespace trm::core;

namespace {

const RunwayEnd* FindEnd(const Airport& airport, const std::string& id)
{
    for (const auto& rwyEnd : airport.runways) {
        if (rwyEnd.id == id) {
            return &rwyEnd;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("ParseAptDat reads a land airport header and runway pair", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "100 45.00 1 0 0 0 0 0 09 33.9000 -118.4000 0 0 0 0 0 0 27 33.9100 -118.3800 0 0 0 0 0 0\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);

    REQUIRE(db.count("KTST") == 1);
    const Airport& airport = db.at("KTST");
    CHECK(airport.icao == "KTST");
    CHECK(airport.name == "Test Airport");
    CHECK(airport.elevation_ft == Approx(13.0));
    REQUIRE(airport.runways.size() == 2);

    const RunwayEnd* end09 = FindEnd(airport, "09");
    const RunwayEnd* end27 = FindEnd(airport, "27");
    REQUIRE(end09 != nullptr);
    REQUIRE(end27 != nullptr);

    CHECK(end09->lat_deg == Approx(33.9000));
    CHECK(end09->lon_deg == Approx(-118.4000));
    CHECK(end09->other_end_id == "27");
    CHECK(end27->lat_deg == Approx(33.9100));
    CHECK(end27->lon_deg == Approx(-118.3800));
    CHECK(end27->other_end_id == "09");

    // Reciprocal headings.
    CHECK(end27->heading_deg == Approx(std::fmod(end09->heading_deg + 180.0, 360.0)).margin(1e-6));

    // Both ends of one physical runway share the same derived length, which
    // should match an independent GreatCircleDistanceNm computation between
    // the two thresholds.
    const double expectedLengthFt =
        GreatCircleDistanceNm(end09->lat_deg, end09->lon_deg, end27->lat_deg, end27->lon_deg) * kNmToFt;
    CHECK(end09->length_ft == Approx(expectedLengthFt));
    CHECK(end27->length_ft == Approx(expectedLengthFt));

    // Reference point = centroid of the runway thresholds.
    REQUIRE(airport.HasReferencePoint());
    CHECK(airport.ref_lat_deg == Approx((33.9000 + 33.9100) / 2.0));
    CHECK(airport.ref_lon_deg == Approx((-118.4000 + -118.3800) / 2.0));

    CHECK(airport.IsSingleRunwayAirport());
}

TEST_CASE("Airport::IsSingleRunwayAirport is false once a second physical runway is parsed", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "100 45.00 1 0 0 0 0 0 09 33.9000 -118.4000 0 0 0 0 0 0 27 33.9100 -118.3800 0 0 0 0 0 0\n"
        "100 45.00 1 0 0 0 0 0 18 33.9050 -118.4050 0 0 0 0 0 0 36 33.9150 -118.3950 0 0 0 0 0 0\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);

    REQUIRE(db.count("KTST") == 1);
    const Airport& airport = db.at("KTST");
    REQUIRE(airport.runways.size() == 4);
    CHECK_FALSE(airport.IsSingleRunwayAirport());
}

TEST_CASE("ParseAptDat: row 1 with no name tokens leaves name empty, not a crash", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);

    REQUIRE(db.count("KTST") == 1);
    CHECK(db.at("KTST").name.empty());
}

TEST_CASE("ParseAptDat: row 99 stops parsing immediately", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "99\n"
        "100 45.00 1 0 0 0 0 0 09 33.9000 -118.4000 0 0 0 0 0 0 27 33.9100 -118.3800 0 0 0 0 0 0\n");

    const AirportDatabase db = ParseAptDat(in);

    REQUIRE(db.count("KTST") == 1);
    CHECK(db.at("KTST").runways.empty());
    CHECK_FALSE(db.at("KTST").HasReferencePoint());
}

TEST_CASE("ParseAptDat: airport header with no runways has no reference point", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);

    REQUIRE(db.count("KTST") == 1);
    CHECK(db.at("KTST").runways.empty());
    CHECK_FALSE(db.at("KTST").HasReferencePoint());
}

TEST_CASE("ParseAptDat: malformed row 1 (missing ICAO token) clears airport context", "[AptDat]")
{
    // Only 3 tokens -- no token 5, so this must NOT start a new airport
    // context; the following row 100 must be silently dropped, not
    // attributed to some stale previous airport.
    std::istringstream in(
        "1 13 0\n"
        "100 45.00 1 0 0 0 0 0 09 33.9000 -118.4000 0 0 0 0 0 0 27 33.9100 -118.3800 0 0 0 0 0 0\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);
    CHECK(db.empty());
}

TEST_CASE("ParseAptDat: multiple airports each get their own runway list", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test One\n"
        "100 45.00 1 0 0 0 0 0 09 33.9000 -118.4000 0 0 0 0 0 0 27 33.9100 -118.3800 0 0 0 0 0 0\n"
        "1 500 0 0 KTS2 Test Two\n"
        "100 30.00 1 0 0 0 0 0 18 40.0000 -100.0000 0 0 0 0 0 0 36 40.0200 -100.0000 0 0 0 0 0 0\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);

    REQUIRE(db.count("KTST") == 1);
    REQUIRE(db.count("KTS2") == 1);
    CHECK(db.at("KTST").runways.size() == 2);
    CHECK(db.at("KTS2").runways.size() == 2);
    CHECK(db.at("KTS2").elevation_ft == Approx(500.0));
}

namespace {

AirportDatabase MakeTwoAirportDb()
{
    std::istringstream in(
        "1 13 0 0 KTST Test One\n"
        "100 45.00 1 0 0 0 0 0 09 0.0000 0.0000 0 0 0 0 0 0 27 0.0000 1.0000 0 0 0 0 0 0\n"
        "1 500 0 0 KFAR Test Far\n"
        "100 30.00 1 0 0 0 0 0 18 40.0000 -100.0000 0 0 0 0 0 0 36 40.0200 -100.0000 0 0 0 0 0 0\n"
        "99\n");
    return ParseAptDat(in);
}

} // namespace

TEST_CASE("FindNearestAirports: returns airports within radius, nearest first", "[AptDat]")
{
    const AirportDatabase db = MakeTwoAirportDb();

    // KTST's centroid is ~(0.0, 0.5); querying from (0,0) should find it
    // comfortably within a small radius, while KFAR (near 40N/100W) should
    // not.
    const auto results = FindNearestAirports(db, 0.0, 0.0, 50.0);
    REQUIRE(results.size() == 1);
    CHECK(results[0].icao == "KTST");
    CHECK(results[0].name == "Test One");
}

TEST_CASE("FindNearestAirports: excludes airports with no reference point", "[AptDat]")
{
    AirportDatabase db;
    Airport noRunways;
    noRunways.icao = "KEMP";
    db["KEMP"] = noRunways;

    CHECK(FindNearestAirports(db, 0.0, 0.0, 1000.0).empty());
}

TEST_CASE("AirportDistanceNm: known airport with a reference point", "[AptDat]")
{
    const AirportDatabase db = MakeTwoAirportDb();
    const auto distance = AirportDistanceNm(db, "KTST", 0.0, 0.0);
    REQUIRE(distance.has_value());
    CHECK(*distance > 0.0);
}

TEST_CASE("AirportDistanceNm: unknown icao or no reference point returns nullopt", "[AptDat]")
{
    const AirportDatabase db = MakeTwoAirportDb();
    CHECK_FALSE(AirportDistanceNm(db, "KXXX", 0.0, 0.0).has_value());

    AirportDatabase dbWithEmpty = db;
    Airport noRunways;
    noRunways.icao = "KEMP";
    dbWithEmpty["KEMP"] = noRunways;
    CHECK_FALSE(AirportDistanceNm(dbWithEmpty, "KEMP", 0.0, 0.0).has_value());
}

TEST_CASE("FindRunwayLengthFt: finds by id, nullopt if absent", "[AptDat]")
{
    const AirportDatabase db = MakeTwoAirportDb();
    const Airport& airport = db.at("KTST");

    const auto length = FindRunwayLengthFt(airport, "09");
    REQUIRE(length.has_value());
    CHECK(*length > 0.0);

    CHECK_FALSE(FindRunwayLengthFt(airport, "99").has_value());
}

TEST_CASE("MergeAirportDatabases: higher-priority database wins for a shared ICAO", "[AptDat]")
{
    std::istringstream defaultIn("1 0 0 0 KTST Default Version\n99\n");
    const AirportDatabase defaultDb = ParseAptDat(defaultIn);

    std::istringstream customIn("1 0 0 0 KTST Custom Version\n99\n");
    const AirportDatabase customDb = ParseAptDat(customIn);

    const AirportDatabase merged = MergeAirportDatabases({&customDb, &defaultDb});

    REQUIRE(merged.count("KTST") == 1);
    CHECK(merged.at("KTST").name == "Custom Version");
}

TEST_CASE("MergeAirportDatabases: falls through to a lower-priority database for ICAOs it doesn't define", "[AptDat]")
{
    std::istringstream customIn("1 0 0 0 KJFK Custom JFK\n99\n");
    const AirportDatabase customDb = ParseAptDat(customIn);
    const AirportDatabase defaultDb = MakeTwoAirportDb();

    const AirportDatabase merged = MergeAirportDatabases({&customDb, &defaultDb});

    REQUIRE(merged.count("KJFK") == 1);
    CHECK(merged.at("KJFK").name == "Custom JFK");
    REQUIRE(merged.count("KTST") == 1);
    CHECK(merged.at("KTST").name == "Test One");
    REQUIRE(merged.count("KFAR") == 1);
}

TEST_CASE("MergeAirportDatabases: null entries are skipped", "[AptDat]")
{
    const AirportDatabase defaultDb = MakeTwoAirportDb();
    const AirportDatabase merged = MergeAirportDatabases({nullptr, &defaultDb});
    CHECK(merged.size() == defaultDb.size());
}

TEST_CASE("MergeAirportDatabases: empty list yields an empty database", "[AptDat]")
{
    CHECK(MergeAirportDatabases({}).empty());
}

// --- ATC traffic flow rows (1000-1004, 1100/1110) -----------------------

TEST_CASE("ParseAptDat reads a full traffic flow block", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "100 45.00 1 0 0 0 0 0 09 33.9000 -118.4000 0 0 0 0 0 0 27 33.9100 -118.3800 0 0 0 0 0 0\n"
        "1000 Westerly Flow south landing\n"
        "1001 KTST 270 000 99\n"
        "1002 KTST 200\n"
        "1003 KTST 0.5\n"
        "1004 2000 0500\n"
        "1101 27 right\n"
        "1110 27 118825 arrivals|departures heavy|jets|props 000000 190290 Some Rule Name\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);
    const Airport& airport = db.at("KTST");

    REQUIRE(airport.flows.size() == 1);
    const TrafficFlow& flow = airport.flows.front();
    CHECK(flow.name == "Westerly Flow south landing");

    REQUIRE(flow.wind_rules.size() == 1);
    CHECK(flow.wind_rules[0].dir_min_deg == Approx(270.0));
    CHECK(flow.wind_rules[0].dir_max_deg == Approx(0.0));
    CHECK(flow.wind_rules[0].max_speed_kt == Approx(99.0));

    CHECK(flow.min_ceiling_ft == Approx(200.0));
    CHECK(flow.min_visibility_sm == Approx(0.5));

    REQUIRE(flow.time_rule.has_value());
    CHECK(flow.time_rule->start_utc_minutes == 20 * 60);
    CHECK(flow.time_rule->end_utc_minutes == 5 * 60);

    REQUIRE(flow.runway_use_rules.size() == 1);
    const FlowRunwayUseRule& rule = flow.runway_use_rules.front();
    CHECK(rule.runway_id == "27");
    CHECK(rule.arrivals);
    CHECK(rule.departures);
    CHECK(rule.aircraft_classes == std::vector<std::string>{"heavy", "jets", "props"});
}

TEST_CASE("ParseAptDat: multiple wind rules in one flow are all kept, in file order", "[AptDat]")
{
    // The real LEPA/KSEA/LEMD shape. Keeping only the last would drop the
    // leading calm-wind clause -- see TrafficFlow::wind_rules.
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "1000 Calm and South flow\n"
        "1001 KTST 000 359 5\n"
        "1001 KTST 070 250 999\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);
    const Airport& airport = db.at("KTST");

    REQUIRE(airport.flows.size() == 1);
    REQUIRE(airport.flows[0].wind_rules.size() == 2);
    CHECK(airport.flows[0].wind_rules[0].max_speed_kt == Approx(5.0));
    CHECK(airport.flows[0].wind_rules[1].max_speed_kt == Approx(999.0));
}

TEST_CASE("ParseAptDat: row 1100 is accepted as an alias for row 1110", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "1000 Only flow\n"
        "1100 09 11920 arrivals jets 000000 000000\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);
    const Airport& airport = db.at("KTST");
    REQUIRE(airport.flows.size() == 1);
    REQUIRE(airport.flows[0].runway_use_rules.size() == 1);
    CHECK(airport.flows[0].runway_use_rules[0].runway_id == "09");
    CHECK(airport.flows[0].runway_use_rules[0].arrivals);
    CHECK_FALSE(airport.flows[0].runway_use_rules[0].departures);
}

TEST_CASE("ParseAptDat: flows are kept in file order and rows attach to the flow that opened them",
          "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "1000 First\n"
        "1001 KTST 000 180 99\n"
        "1110 09 118825 arrivals jets 000000 000000\n"
        "1000 Second\n"
        "1110 27 118825 departures jets 000000 000000\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);
    const Airport& airport = db.at("KTST");

    REQUIRE(airport.flows.size() == 2);
    CHECK(airport.flows[0].name == "First");
    CHECK(airport.flows[1].name == "Second");
    CHECK(airport.flows[0].wind_rules.size() == 1);
    CHECK(airport.flows[1].wind_rules.empty());
    REQUIRE(airport.flows[0].runway_use_rules.size() == 1);
    CHECK(airport.flows[0].runway_use_rules[0].runway_id == "09");
    REQUIRE(airport.flows[1].runway_use_rules.size() == 1);
    CHECK(airport.flows[1].runway_use_rules[0].runway_id == "27");
}

TEST_CASE("ParseAptDat: a flow block does not leak across an airport boundary", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KONE Airport One\n"
        "1000 One's flow\n"
        "1 20 0 0 KTWO Airport Two\n"
        "1110 09 118825 arrivals jets 000000 000000\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);

    REQUIRE(db.at("KONE").flows.size() == 1);
    // The orphaned 1110 belongs to no flow of KTWO's, so it is dropped rather
    // than appended to KONE's still-open block.
    CHECK(db.at("KONE").flows[0].runway_use_rules.empty());
    CHECK(db.at("KTWO").flows.empty());
}

TEST_CASE("ParseAptDat: airports with no flow rows have an empty flow list", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "100 45.00 1 0 0 0 0 0 09 33.9000 -118.4000 0 0 0 0 0 0 27 33.9100 -118.3800 0 0 0 0 0 0\n"
        "99\n");

    CHECK(ParseAptDat(in).at("KTST").flows.empty());
}

TEST_CASE("ParseAptDat: a malformed time rule leaves the flow unrestricted by time", "[AptDat]")
{
    std::istringstream in(
        "1 13 0 0 KTST Test Airport\n"
        "1000 Only flow\n"
        "1004 8 2200\n"
        "99\n");

    const AirportDatabase db = ParseAptDat(in);
    const Airport& airport = db.at("KTST");
    REQUIRE(airport.flows.size() == 1);
    CHECK_FALSE(airport.flows[0].time_rule.has_value());
}
