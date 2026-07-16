#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "core/AptDat.h"
#include "core/GeoMath.h"
#include "core/RunwayMatcher.h"

using namespace trm::core;

namespace {

// Degrees of latitude (or, at the equator, longitude) that correspond to
// exactly `nm` nautical miles under GreatCircleDistanceNm's own formula --
// exact for a pure meridian/equatorial offset (no haversine approximation
// error), so this is a reliable way to construct exact-boundary fixtures
// against the tolerances under test.
double DegForNm(double nm)
{
    const double pi = std::acos(-1.0);
    return nm / kEarthRadiusNm * (180.0 / pi);
}

Airport MakeSingleRunwayAirport()
{
    Airport airport;
    airport.icao = "KTST";
    airport.runways.push_back(RunwayEnd{"09", 0.0, 0.0, 90.0, 45.0, "27", 5000.0});
    return airport;
}

} // namespace

TEST_CASE("MatchRunwayEnd: heading tolerance boundary", "[RunwayMatcher]")
{
    const Airport airport = MakeSingleRunwayAirport();
    // West of the threshold, exactly on the centerline (cross-track == 0),
    // ~3nm out (comfortably inside the 8nm along-track limit) -- isolates
    // the heading check from the other two.
    const double acLat = 0.0;
    const double acLon = -0.05;

    SECTION("exact heading match") {
        CHECK(MatchRunwayEnd(airport, acLat, acLon, 90.0) != nullptr);
    }
    SECTION("exactly at tolerance (+15)") {
        CHECK(MatchRunwayEnd(airport, acLat, acLon, 105.0) != nullptr);
    }
    SECTION("just inside tolerance (+14.99)") {
        CHECK(MatchRunwayEnd(airport, acLat, acLon, 104.99) != nullptr);
    }
    SECTION("just outside tolerance (+15.01)") {
        CHECK(MatchRunwayEnd(airport, acLat, acLon, 105.01) == nullptr);
    }
    SECTION("exactly at tolerance (-15)") {
        CHECK(MatchRunwayEnd(airport, acLat, acLon, 75.0) != nullptr);
    }
    SECTION("just outside tolerance (-15.01)") {
        CHECK(MatchRunwayEnd(airport, acLat, acLon, 74.99) == nullptr);
    }
    SECTION("far outside (opposite heading)") {
        CHECK(MatchRunwayEnd(airport, acLat, acLon, 270.0) == nullptr);
    }
}

TEST_CASE("MatchRunwayEnd: lateral (cross-track) tolerance boundary", "[RunwayMatcher]")
{
    const Airport airport = MakeSingleRunwayAirport();
    // Same longitude as the threshold (0.0) so along-track ~= 0 and the
    // pure-latitude offset below maps directly to cross-track distance;
    // heading matches exactly so only the lateral check is exercised.
    const double acLon = 0.0;
    const double acHeading = 90.0;

    // "Exactly at" uses tolerance - 1e-6nm rather than the literal
    // mathematical boundary: GreatCircleDistanceNm chains division/trig/asin
    // operations, so the true boundary value can land a ~1e-14nm floating
    // residue on either side of the comparison -- a real flakiness risk for
    // a strict `<=`. 1e-6nm (~0.06 inches) is still well inside the
    // tolerance for any practical purpose, but five orders of magnitude
    // larger than that residue, so it's a safe stand-in for "at the edge".
    SECTION("at tolerance (0.5nm north, to within 1e-6nm)") {
        CHECK(MatchRunwayEnd(airport, DegForNm(0.5 - 1e-6), acLon, acHeading) != nullptr);
    }
    SECTION("just inside tolerance (0.499nm north)") {
        CHECK(MatchRunwayEnd(airport, DegForNm(0.499), acLon, acHeading) != nullptr);
    }
    SECTION("just outside tolerance (0.501nm north)") {
        CHECK(MatchRunwayEnd(airport, DegForNm(0.501), acLon, acHeading) == nullptr);
    }
    SECTION("at tolerance (0.5nm south, to within 1e-6nm)") {
        CHECK(MatchRunwayEnd(airport, -DegForNm(0.5 - 1e-6), acLon, acHeading) != nullptr);
    }
    SECTION("just outside tolerance (0.501nm south)") {
        CHECK(MatchRunwayEnd(airport, -DegForNm(0.501), acLon, acHeading) == nullptr);
    }
}

TEST_CASE("MatchRunwayEnd: along-track / max-distance tolerance boundary", "[RunwayMatcher]")
{
    const Airport airport = MakeSingleRunwayAirport();
    // Due west of the threshold, on the centerline (lat == 0), heading
    // matches exactly -- isolates the max_along_track_nm pre-filter, which
    // compares straight-line distance, not the along-track decomposition.
    const double acLat = 0.0;
    const double acHeading = 90.0;

    SECTION("at tolerance (8nm, to within 1e-6nm)") {
        CHECK(MatchRunwayEnd(airport, acLat, -DegForNm(8.0 - 1e-6), acHeading) != nullptr);
    }
    SECTION("just inside tolerance (7.999nm)") {
        CHECK(MatchRunwayEnd(airport, acLat, -DegForNm(7.999), acHeading) != nullptr);
    }
    SECTION("just outside tolerance (8.001nm)") {
        CHECK(MatchRunwayEnd(airport, acLat, -DegForNm(8.001), acHeading) == nullptr);
    }
}

TEST_CASE("MatchRunwayEnd: multiple candidates, closest along-track wins", "[RunwayMatcher]")
{
    Airport airport;
    airport.icao = "KTST";
    airport.runways.push_back(RunwayEnd{"09A", 0.0, 0.0, 90.0, 45.0, "27A", 5000.0});
    airport.runways.push_back(RunwayEnd{"09B", 0.0, -0.03, 90.0, 45.0, "27B", 5000.0});

    // Aircraft west of both thresholds, on the shared centerline, matching
    // heading for both -- 09B is closer (smaller along-track distance).
    const RunwayEnd* matched = MatchRunwayEnd(airport, 0.0, -0.05, 90.0);
    REQUIRE(matched != nullptr);
    CHECK(matched->id == "09B");
}

TEST_CASE("MatchRunwayEnd: no runways at all returns nullptr", "[RunwayMatcher]")
{
    Airport airport;
    airport.icao = "KEMP";
    CHECK(MatchRunwayEnd(airport, 0.0, 0.0, 90.0) == nullptr);
}
