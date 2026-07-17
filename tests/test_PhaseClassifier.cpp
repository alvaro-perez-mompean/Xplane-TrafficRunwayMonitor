#include <catch2/catch_test_macros.hpp>

#include "core/PhaseClassifier.h"

using namespace trm::core;

namespace {
constexpr double kOnGroundAgl = 2.0;   // < 5.0 threshold
constexpr double kAirborneAgl = 300.0; // > 5.0 threshold, <= 500.0 windows
constexpr double kHighAgl = 800.0;     // past both initial_climb/final_approach windows
} // namespace

TEST_CASE("ClassifyPhase: on-ground branches", "[PhaseClassifier]")
{
    SECTION("aligned, fast, increasing -> takeoff_roll") {
        CHECK(ClassifyPhase(kOnGroundAgl, 50.0, VsState::kStable, true, GsTrend::kIncreasing)
              == FlightPhase::kTakeoffRoll);
    }
    SECTION("aligned, fast, decreasing -> landing_rollout") {
        CHECK(ClassifyPhase(kOnGroundAgl, 50.0, VsState::kStable, true, GsTrend::kDecreasing)
              == FlightPhase::kLandingRollout);
    }
    SECTION("not aligned -> taxi, regardless of speed/trend") {
        CHECK(ClassifyPhase(kOnGroundAgl, 50.0, VsState::kStable, false, GsTrend::kIncreasing)
              == FlightPhase::kTaxi);
    }
    SECTION("aligned but slow (<= 40kt) -> taxi") {
        CHECK(ClassifyPhase(kOnGroundAgl, 20.0, VsState::kStable, true, GsTrend::kIncreasing)
              == FlightPhase::kTaxi);
    }
    SECTION("aligned, fast, but trend stable -> taxi") {
        CHECK(ClassifyPhase(kOnGroundAgl, 50.0, VsState::kStable, true, GsTrend::kStable)
              == FlightPhase::kTaxi);
    }
}

TEST_CASE("ClassifyPhase: airborne branches", "[PhaseClassifier]")
{
    SECTION("aligned, climbing, fast, within window -> initial_climb") {
        CHECK(ClassifyPhase(kAirborneAgl, 90.0, VsState::kClimbing, true, GsTrend::kStable)
              == FlightPhase::kInitialClimb);
    }
    SECTION("aligned, descending, within window -> final_approach") {
        CHECK(ClassifyPhase(kAirborneAgl, 90.0, VsState::kDescending, true, GsTrend::kStable)
              == FlightPhase::kFinalApproach);
    }
    SECTION("climbing but not aligned -> departing") {
        CHECK(ClassifyPhase(kAirborneAgl, 90.0, VsState::kClimbing, false, GsTrend::kStable)
              == FlightPhase::kDeparting);
    }
    SECTION("climbing, aligned, but past the initial-climb AGL window -> departing") {
        CHECK(ClassifyPhase(kHighAgl, 90.0, VsState::kClimbing, true, GsTrend::kStable)
              == FlightPhase::kDeparting);
    }
    SECTION("climbing, aligned, within AGL window, but too slow -> departing") {
        CHECK(ClassifyPhase(kAirborneAgl, 50.0, VsState::kClimbing, true, GsTrend::kStable)
              == FlightPhase::kDeparting);
    }
    SECTION("descending but not aligned -> arriving") {
        CHECK(ClassifyPhase(kAirborneAgl, 90.0, VsState::kDescending, false, GsTrend::kStable)
              == FlightPhase::kArriving);
    }
    SECTION("descending, aligned, but past the final-approach AGL window -> arriving") {
        CHECK(ClassifyPhase(kHighAgl, 90.0, VsState::kDescending, true, GsTrend::kStable)
              == FlightPhase::kArriving);
    }
    SECTION("neither climbing nor descending -> airborne_enroute") {
        CHECK(ClassifyPhase(kHighAgl, 250.0, VsState::kStable, false, GsTrend::kStable)
              == FlightPhase::kAirborneEnroute);
    }
}

TEST_CASE("ClassifyPhase: on-ground/airborne boundary uses strict less-than on AGL", "[PhaseClassifier]")
{
    PhaseClassifierConfig config;
    // AGL exactly at the ground threshold (5.0) is airborne (on_ground uses
    // `agl_m < threshold`, not `<=`).
    CHECK(ClassifyPhase(config.ground_agl_threshold_m, 90.0, VsState::kStable, false, GsTrend::kStable, config)
          == FlightPhase::kAirborneEnroute);
}

TEST_CASE("ClassifyPhase: low groundspeed overrides a wrong (too-high) AGL reading", "[PhaseClassifier]")
{
    // Reproduces the LEMD/LEBL in-sim finding: a terrain probe under some
    // custom scenery reports real but unflattened (too-high) AGL for a
    // genuinely grounded aircraft. A sustained low groundspeed can't be
    // real flight for fixed-wing traffic, so it overrides AGL.
    SECTION("stationary despite airborne-looking AGL -> taxi") {
        CHECK(ClassifyPhase(kAirborneAgl, 0.0, VsState::kStable, true, GsTrend::kStable)
              == FlightPhase::kTaxi);
    }
    SECTION("slow taxi despite airborne-looking AGL, not aligned -> taxi") {
        CHECK(ClassifyPhase(kAirborneAgl, 5.0, VsState::kStable, false, GsTrend::kStable)
              == FlightPhase::kTaxi);
    }
}

TEST_CASE("ClassifyPhase: ground_gs_override_kt boundary uses strict less-than", "[PhaseClassifier]")
{
    PhaseClassifierConfig config;
    SECTION("gs exactly at override threshold -> not forced on-ground") {
        CHECK(ClassifyPhase(kAirborneAgl, config.ground_gs_override_kt, VsState::kStable, false, GsTrend::kStable,
                             config)
              == FlightPhase::kAirborneEnroute);
    }
    SECTION("gs just below override threshold -> forced on-ground (taxi)") {
        CHECK(ClassifyPhase(kAirborneAgl, config.ground_gs_override_kt - 0.001, VsState::kStable, false,
                             GsTrend::kStable, config)
              == FlightPhase::kTaxi);
    }
}
