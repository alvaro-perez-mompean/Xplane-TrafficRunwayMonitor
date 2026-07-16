#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/GeoMath.h"

using Catch::Approx;
using namespace trm::core;

TEST_CASE("AngleDiffDeg wraps to the signed shortest angle", "[GeoMath]")
{
    CHECK(AngleDiffDeg(0.0, 0.0) == Approx(0.0));
    CHECK(AngleDiffDeg(350.0, 10.0) == Approx(-20.0));
    CHECK(AngleDiffDeg(10.0, 350.0) == Approx(20.0));
    CHECK(AngleDiffDeg(180.0, 0.0) == Approx(-180.0));
    CHECK(AngleDiffDeg(100.0, 95.0) == Approx(5.0));
}

TEST_CASE("InitialBearingDeg returns cardinal directions", "[GeoMath]")
{
    CHECK(InitialBearingDeg(0.0, 0.0, 1.0, 0.0) == Approx(0.0).margin(1e-6));   // north
    CHECK(InitialBearingDeg(0.0, 0.0, 0.0, 1.0) == Approx(90.0).margin(1e-6));  // east
    CHECK(InitialBearingDeg(0.0, 0.0, -1.0, 0.0) == Approx(180.0).margin(1e-6)); // south
    CHECK(InitialBearingDeg(0.0, 0.0, 0.0, -1.0) == Approx(270.0).margin(1e-6)); // west
}

TEST_CASE("GreatCircleDistanceNm matches the nm-per-degree-of-latitude convention", "[GeoMath]")
{
    // 1 degree of latitude is ~60nm by construction (nautical mile = 1
    // minute of arc); the earth radius constant here should reproduce that.
    const double oneDegLatNm = GreatCircleDistanceNm(0.0, 0.0, 1.0, 0.0);
    CHECK(oneDegLatNm == Approx(60.045).margin(0.01));

    CHECK(GreatCircleDistanceNm(10.0, 20.0, 10.0, 20.0) == Approx(0.0).margin(1e-9));

    // Symmetric.
    const double ab = GreatCircleDistanceNm(10.0, 20.0, 15.0, 25.0);
    const double ba = GreatCircleDistanceNm(15.0, 25.0, 10.0, 20.0);
    CHECK(ab == Approx(ba));
}

TEST_CASE("LocalOffsetFromReference places cardinal directions on the correct axis/sign", "[GeoMath]")
{
    const auto north = LocalOffsetFromReference(1.0, 0.0, 0.0, 0.0);
    CHECK(north.north_ft > 0.0);
    CHECK(north.east_ft == Approx(0.0).margin(1e-6));

    const auto south = LocalOffsetFromReference(-1.0, 0.0, 0.0, 0.0);
    CHECK(south.north_ft < 0.0);
    CHECK(south.north_ft == Approx(-north.north_ft));

    const auto east = LocalOffsetFromReference(0.0, 1.0, 0.0, 0.0);
    CHECK(east.east_ft > 0.0);
    CHECK(east.north_ft == Approx(0.0).margin(1e-6));

    // Magnitude should agree with the great-circle distance for a due-north
    // offset (no longitude scaling to complicate it).
    const double oneDegLatFt = GreatCircleDistanceNm(0.0, 0.0, 1.0, 0.0) * kNmToFt;
    CHECK(north.north_ft == Approx(oneDegLatFt).margin(1.0));

    // The reference point itself offsets to the origin.
    const auto origin = LocalOffsetFromReference(51.5, -0.1, 51.5, -0.1);
    CHECK(origin.east_ft == Approx(0.0).margin(1e-9));
    CHECK(origin.north_ft == Approx(0.0).margin(1e-9));
}
