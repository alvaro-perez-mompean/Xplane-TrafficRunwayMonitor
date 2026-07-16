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
