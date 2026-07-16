#pragma once

#include "core/AptDat.h"

// Runway matching: given an aircraft's position + true heading, find which
// runway end (if any) of an airport it's aligned with.

namespace trm::core {

struct RunwayMatchConfig {
    double heading_tolerance_deg = 15.0;
    double lateral_tolerance_nm = 0.5;
    double max_along_track_nm = 8.0;
};

// A match requires all three: heading within tolerance of the runway end's
// own heading, lateral (cross-track) distance from the extended centerline
// within tolerance, and straight-line distance from the threshold within
// max_along_track_nm. If several runway ends match, the closest by
// along-track distance wins. Returns nullptr if none match.
//
// The returned pointer aliases into `airport.runways` and is only valid as
// long as `airport` is not modified or destroyed.
const RunwayEnd* MatchRunwayEnd(const Airport& airport, double acLatDeg, double acLonDeg,
                                 double acHeadingTrueDeg, const RunwayMatchConfig& config = {});

} // namespace trm::core
