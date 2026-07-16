#pragma once

#include "core/TrendFilter.h"

// Flight-phase state machine.
//
// Only takeoff_roll/initial_climb (departure) and final_approach/
// landing_rollout (arrival) feed the sighting system (SightingTracker) --
// taxi/departing/arriving/airborne_enroute are classified but not scored.

namespace trm::core {

enum class FlightPhase {
    kTaxi,
    kTakeoffRoll,
    kInitialClimb,
    kDeparting,
    kArriving,
    kFinalApproach,
    kLandingRollout,
    kAirborneEnroute,
};

struct PhaseClassifierConfig {
    double ground_agl_threshold_m = 5.0;
    double takeoff_roll_min_gs_kt = 40.0;
    double initial_climb_min_gs_kt = 80.0;
    double initial_climb_max_agl_m = 500.0;
    double final_approach_max_agl_m = 500.0;
};

// `aligned` mirrors "a runway end matched" (RunwayMatcher) --
// this function only ever needs the yes/no fact, not the RunwayEnd itself.
// gs_trend/vs_state are assumed already hysteresis-filtered by TrendFilter;
// this function does no filtering of its own.
FlightPhase ClassifyPhase(double aglM, double gsKt, VsState vsState, bool aligned, GsTrend gsTrend,
                           const PhaseClassifierConfig& config = {});

} // namespace trm::core
