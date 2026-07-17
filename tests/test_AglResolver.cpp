#include <catch2/catch_test_macros.hpp>

#include "core/AglResolver.h"

using namespace trm::core;

namespace {
constexpr double kAirportElevationFt = 500.0;         // 152.4 m MSL
constexpr double kAirportElevationMslM = 152.4;
constexpr double kAircraftMslM = 300.0;
} // namespace

TEST_CASE("ResolveAgl: no probe reading falls back to airport elevation", "[AglResolver]")
{
    const AglResult result = ResolveAgl(kAircraftMslM, std::nullopt, kAirportElevationFt);
    CHECK(result.source == AglSource::kAirportElevation);
    CHECK(result.agl_m == kAircraftMslM - kAirportElevationMslM);
}

TEST_CASE("ResolveAgl: probe reading close to airport elevation is trusted", "[AglResolver]")
{
    const double probeElevationMslM = kAirportElevationMslM + 10.0; // nearby terrain, plausible
    const AglResult result = ResolveAgl(kAircraftMslM, probeElevationMslM, kAirportElevationFt);
    CHECK(result.source == AglSource::kTerrainProbe);
    CHECK(result.agl_m == kAircraftMslM - probeElevationMslM);
}

TEST_CASE("ResolveAgl: probe reading far from airport elevation is distrusted", "[AglResolver]")
{
    // Simulates the documented out-of-loaded-scenery failure mode: a
    // "successful" xplm_ProbeHitTerrain at a bogus 0 MSL sphere, checked
    // against a high-elevation airport so the disagreement actually clears
    // the default threshold (a low-elevation airport's own charted
    // elevation could itself be within a few hundred meters of 0 MSL,
    // which wouldn't exercise this branch at all).
    constexpr double kHighAirportElevationFt = 8000.0; // 2438.4 m MSL
    constexpr double kHighAircraftMslM = 2600.0;
    const double bogusProbeElevationMslM = 0.0;
    const AglResult result = ResolveAgl(kHighAircraftMslM, bogusProbeElevationMslM, kHighAirportElevationFt);
    CHECK(result.source == AglSource::kAirportElevation);
    CHECK(result.agl_m == kHighAircraftMslM - kHighAirportElevationFt * 0.3048);
}

TEST_CASE("ResolveAgl: disagreement threshold is inclusive at the boundary", "[AglResolver]")
{
    AglResolverConfig config;
    config.max_disagreement_m = 100.0;

    SECTION("exactly at threshold -> trusted") {
        const double probeElevationMslM = kAirportElevationMslM + 100.0;
        const AglResult result = ResolveAgl(kAircraftMslM, probeElevationMslM, kAirportElevationFt, config);
        CHECK(result.source == AglSource::kTerrainProbe);
    }
    SECTION("just past threshold -> distrusted") {
        const double probeElevationMslM = kAirportElevationMslM + 100.001;
        const AglResult result = ResolveAgl(kAircraftMslM, probeElevationMslM, kAirportElevationFt, config);
        CHECK(result.source == AglSource::kAirportElevation);
    }
}
