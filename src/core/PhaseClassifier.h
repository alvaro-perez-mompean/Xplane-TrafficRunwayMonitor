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

    // Independent, scenery-agnostic fallback for "on the ground", alongside
    // ground_agl_threshold_m. A real terrain probe under some custom
    // scenery packs can report a real but unflattened-to-pavement reading
    // -- confirmed in-sim at two different airports with two different
    // offsets (LEMD ~100m, LEBL ~43m) -- so no single fixed tolerance in
    // core::ResolveAgl can be trusted to catch every case; the AGL value
    // itself can simply be wrong by an airport-dependent amount. A
    // groundspeed this low can't be sustained flight for fixed-wing
    // traffic (helicopters are excluded from this pipeline entirely -- see
    // sdk::SlotReading::is_helicopter), so it's trusted over AGL.
    // Deliberately well below realistic taxi speeds (which can reach
    // 30-50kt) rather than covering the whole taxi speed range -- every
    // real taxi-out or landing rollout passes through a slow/near-
    // stationary moment at some point, which is all ground_sighting
    // (SightingTracker) needs to be set once.
    double ground_gs_override_kt = 15.0;
};

// `aligned` mirrors "a runway end matched" (RunwayMatcher) --
// this function only ever needs the yes/no fact, not the RunwayEnd itself.
// gs_trend/vs_state are assumed already hysteresis-filtered by TrendFilter;
// this function does no filtering of its own.
FlightPhase ClassifyPhase(double aglM, double gsKt, VsState vsState, bool aligned, GsTrend gsTrend,
                           const PhaseClassifierConfig& config = {});

} // namespace trm::core
