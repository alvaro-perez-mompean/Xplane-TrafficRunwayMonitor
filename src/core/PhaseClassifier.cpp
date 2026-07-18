#include "core/PhaseClassifier.h"

namespace trm::core {

FlightPhase ClassifyPhase(double aglM, double gsKt, VsState vsState, bool aligned, GsTrend gsTrend,
                           const PhaseClassifierConfig& config)
{
    const bool onGround = aglM < config.ground_agl_threshold_m || gsKt < config.ground_gs_override_kt;
    const bool climbing = vsState == VsState::kClimbing;
    const bool descending = vsState == VsState::kDescending;

    if (onGround) {
        if (aligned && gsKt > config.takeoff_roll_min_gs_kt && gsTrend == GsTrend::kIncreasing) {
            return FlightPhase::kTakeoffRoll;
        }
        if (aligned && gsKt > config.takeoff_roll_min_gs_kt && gsTrend == GsTrend::kDecreasing) {
            return FlightPhase::kLandingRollout;
        }
        return FlightPhase::kTaxi;
    }

    // Airborne.
    if (aligned && climbing && gsKt > config.initial_climb_min_gs_kt && aglM <= config.initial_climb_max_agl_m) {
        return FlightPhase::kInitialClimb;
    }
    if (aligned && descending && aglM <= config.final_approach_max_agl_m) {
        return FlightPhase::kFinalApproach;
    }
    if (climbing) {
        return FlightPhase::kDeparting; // climbing, but past the window or not runway-aligned
    }
    if (descending) {
        return FlightPhase::kArriving; // descending toward the area, but not yet established on final
    }
    return FlightPhase::kAirborneEnroute;
}

} // namespace trm::core
