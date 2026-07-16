#include <catch2/catch_test_macros.hpp>

#include "core/TrendFilter.h"

using namespace trm::core;

TEST_CASE("UpdateGsTrend: no previous reading is treated as stable", "[TrendFilter]")
{
    GsTrendState state;
    CHECK(UpdateGsTrend(state, /*havePrevGsKt=*/false, 0.0, 999.0) == GsTrend::kStable);
}

TEST_CASE("UpdateGsTrend: small deltas under the epsilon are ignored", "[TrendFilter]")
{
    GsTrendState state;
    UpdateGsTrend(state, false, 0.0, 50.0); // establish a baseline reading
    // delta = 1.5kt, under the default 2kt epsilon -> raw stays stable.
    CHECK(UpdateGsTrend(state, true, 50.0, 51.5) == GsTrend::kStable);
}

TEST_CASE("UpdateGsTrend: requires trend_confirm_cycles consecutive matching raw samples", "[TrendFilter]")
{
    GsTrendState state;
    UpdateGsTrend(state, false, 0.0, 50.0);

    // Cycle 1 of a real increasing trend: not yet confirmed.
    CHECK(UpdateGsTrend(state, true, 50.0, 60.0) == GsTrend::kStable);
    // Cycle 2: now confirmed.
    CHECK(UpdateGsTrend(state, true, 60.0, 70.0) == GsTrend::kIncreasing);
    // Cycle 3: stays confirmed.
    CHECK(UpdateGsTrend(state, true, 70.0, 80.0) == GsTrend::kIncreasing);
}

TEST_CASE("UpdateGsTrend regression: a single noisy sample surrounded by the opposite "
          "trend must not flip the confirmed value",
          "[TrendFilter]")
{
    // Mirrors the real in-sim incident: a landing rollout's deceleration
    // (confirmed "decreasing") gets one noisy uptick sample, then resumes
    // decreasing. The single blip must never itself read as a confirmed
    // "increasing" trend (which would spuriously suggest takeoff_roll).
    GsTrendState state;
    UpdateGsTrend(state, false, 0.0, 80.0); // baseline

    CHECK(UpdateGsTrend(state, true, 80.0, 75.0) == GsTrend::kStable);      // decreasing, cycle 1
    CHECK(UpdateGsTrend(state, true, 75.0, 70.0) == GsTrend::kDecreasing); // decreasing, cycle 2: confirmed

    // Noisy uptick: delta = +4kt, raw = increasing, but only 1 cycle so far.
    CHECK(UpdateGsTrend(state, true, 70.0, 74.0) == GsTrend::kStable);

    // Decreasing resumes: raw direction changed again, so this also needs
    // to re-accumulate its own streak from scratch.
    CHECK(UpdateGsTrend(state, true, 74.0, 70.0) == GsTrend::kStable);       // decreasing, cycle 1 (post-blip)
    CHECK(UpdateGsTrend(state, true, 70.0, 65.0) == GsTrend::kDecreasing); // decreasing, cycle 2: re-confirmed
}

TEST_CASE("UpdateVsState: requires trend_confirm_cycles consecutive matching raw samples", "[TrendFilter]")
{
    VsStateFilterState state;
    CHECK(UpdateVsState(state, 2.0) == VsState::kStable);       // climbing, cycle 1: not yet confirmed
    CHECK(UpdateVsState(state, 1.5) == VsState::kClimbing);     // climbing, cycle 2: confirmed
}

TEST_CASE("UpdateVsState: values inside the climb/descent deadband are stable", "[TrendFilter]")
{
    VsStateFilterState state;
    CHECK(UpdateVsState(state, 0.0) == VsState::kStable);
    CHECK(UpdateVsState(state, 0.3) == VsState::kStable);
    CHECK(UpdateVsState(state, -0.3) == VsState::kStable);
}

TEST_CASE("UpdateVsState regression: a single noisy sample surrounded by the opposite "
          "trend must not flip the confirmed value",
          "[TrendFilter]")
{
    // Mirrors the real in-sim incident: a real departure's climb gets one
    // negative-vs blip right after liftoff (velocity-interpolation noise),
    // which must never itself read as a confirmed "descending" state (which
    // would spuriously suggest final_approach).
    VsStateFilterState state;
    CHECK(UpdateVsState(state, 2.0) == VsState::kStable);   // climbing, cycle 1
    CHECK(UpdateVsState(state, 1.5) == VsState::kClimbing); // climbing, cycle 2: confirmed

    CHECK(UpdateVsState(state, -0.6) == VsState::kStable); // noisy blip: raw = descending, cycle 1 only

    CHECK(UpdateVsState(state, 1.8) == VsState::kStable);   // climbing resumes, cycle 1 (post-blip)
    CHECK(UpdateVsState(state, 2.0) == VsState::kClimbing); // climbing, cycle 2: re-confirmed
}
