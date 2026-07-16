#pragma once

// gs_trend / vs_state hysteresis. Both trends require the same *raw*
// classification to hold for TREND_CONFIRM_CYCLES consecutive cycles
// before PhaseClassifier trusts it -- a single noisy sample can no longer
// flip takeoff_roll/landing_rollout or initial_climb/final_approach by
// itself. This exists to prevent two specific in-sim incidents seen
// during testing: a landing rollout's deceleration briefly registering as
// increasing groundspeed, and a departure's post-liftoff vertical-speed
// noise briefly registering as descending.

namespace trm::core {

enum class GsTrend { kStable, kIncreasing, kDecreasing };
enum class VsState { kStable, kClimbing, kDescending };

struct TrendFilterConfig {
    double gs_trend_epsilon_kt = 2.0;
    double climb_vs_threshold_mps = 0.5;
    double descent_vs_threshold_mps = -0.5;
    int trend_confirm_cycles = 2;
};

// Per-slot hysteresis bookkeeping the caller owns across cycles.
struct GsTrendState {
    GsTrend raw = GsTrend::kStable;
    int streak = 0;
};

struct VsStateFilterState {
    VsState raw = VsState::kStable;
    int streak = 0;
};

// gs_trend comes from the *delta* between consecutive groundspeed readings.
// `havePrevGsKt` guards the case where there's no previous reading yet:
// the raw classification is "stable" for this cycle (there's no delta to
// compute). Updates `state` in place and
// returns the confirmed (hysteresis-filtered) trend.
GsTrend UpdateGsTrend(GsTrendState& state, bool havePrevGsKt, double prevGsKt, double currGsKt,
                      const TrendFilterConfig& config = {});

// vs_state comes from the *raw* per-cycle vertical speed value (not a
// delta), compared directly against the climb/descent thresholds every
// cycle. Updates `state` in place and returns the confirmed trend.
VsState UpdateVsState(VsStateFilterState& state, double vsMps, const TrendFilterConfig& config = {});

} // namespace trm::core
