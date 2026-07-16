#include "core/TrendFilter.h"

namespace trm::core {

GsTrend UpdateGsTrend(GsTrendState& state, bool havePrevGsKt, double prevGsKt, double currGsKt,
                      const TrendFilterConfig& config)
{
    GsTrend rawTrend = GsTrend::kStable;
    if (havePrevGsKt) {
        const double delta = currGsKt - prevGsKt;
        if (delta > config.gs_trend_epsilon_kt) {
            rawTrend = GsTrend::kIncreasing;
        } else if (delta < -config.gs_trend_epsilon_kt) {
            rawTrend = GsTrend::kDecreasing;
        }
    }

    // Compare against *last* cycle's raw value before overwriting it.
    state.streak = (rawTrend == state.raw) ? state.streak + 1 : 1;
    state.raw = rawTrend;

    return (state.streak >= config.trend_confirm_cycles) ? rawTrend : GsTrend::kStable;
}

VsState UpdateVsState(VsStateFilterState& state, double vsMps, const TrendFilterConfig& config)
{
    VsState rawState = VsState::kStable;
    if (vsMps > config.climb_vs_threshold_mps) {
        rawState = VsState::kClimbing;
    } else if (vsMps < config.descent_vs_threshold_mps) {
        rawState = VsState::kDescending;
    }

    state.streak = (rawState == state.raw) ? state.streak + 1 : 1;
    state.raw = rawState;

    return (state.streak >= config.trend_confirm_cycles) ? rawState : VsState::kStable;
}

} // namespace trm::core
