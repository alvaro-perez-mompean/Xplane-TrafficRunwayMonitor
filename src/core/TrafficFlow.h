#pragma once

#include <optional>
#include <string>
#include <vector>

// apt.dat "ATC Flow" data model (rows 1000-1004 and 1100/1110).
//
// This is X-Plane's own authored rule set for which runways are in use at an
// airport under which conditions -- the same data its ATC reads. There is no
// dataref exposing the sim's resulting choice (every sim/atc/* dataref is COM
// radio state), so replicating the rules against this data is the only way to
// agree with it. core::SelectActiveRunway (ActiveRunway.h) is the evaluator;
// this header is just the shape the parser fills in.
//
// Structs only, no logic -- parsed by core::ParseAptDat alongside rows 1/100.

namespace trm::core {

// What to report when a ceiling or visibility measurement isn't available.
// Deliberately generous rather than 0 so a missing reading can never wrongly
// FAIL a flow's minimum: degrading to "this flow is eligible" is the safe
// direction, since the alternative silently skips the flow X-Plane would have
// picked. They live here, next to the rules that consume them, because that is
// the only reason they exist.
inline constexpr double kUnrestrictedCeilingFt = 99999.0;
inline constexpr double kUnrestrictedVisibilitySm = 99.0;

// Row 1001. The airport's wind must blow FROM within [dir_min_deg,
// dir_max_deg] (a sector that may wrap through 360/0, e.g. 340 -> 139) at no
// more than max_speed_kt. 999 is the file's idiom for "any speed".
struct FlowWindRule {
    double dir_min_deg = 0.0;
    double dir_max_deg = 360.0;
    double max_speed_kt = 999.0;
};

// Row 1004, minutes since 0000Z. The window may wrap through midnight
// (start > end, e.g. 2000 -> 0500).
struct FlowTimeRule {
    int start_utc_minutes = 0;
    int end_utc_minutes = 1440;
};

// Row 1100/1110. Both row codes are the same rule at different tower-frequency
// resolutions (10 kHz vs 1 kHz); X-Plane 12's global apt.dat is authored
// entirely at 1 kHz, but both are accepted. The tower frequency itself and the
// two trailing heading ranges (on-course destination sector, initial assigned
// heading) are deliberately not stored: this plugin reports which runway is in
// use, and has no departure-routing decision to hang them off.
struct FlowRunwayUseRule {
    std::string runway_id;
    bool arrivals = false;
    bool departures = false;
    // apt.dat's own class strings: heavy, jets, turboprops, props, helos,
    // fighters. Empty only in malformed data.
    std::vector<std::string> aircraft_classes;
};

// One row-1000 block and everything attached to it, up to the next row 1000 or
// the end of the airport.
//
// Row 1101 (VFR traffic pattern) is deliberately not parsed. It names a pattern
// runway and circuit side, which reads like a usable hint for the flows that
// have no runway-in-use rule covering the operation being asked about, but it
// governs VFR circuit direction, not IFR runway assignment, and using it as one
// would be inventing behavior X-Plane does not have. Those cases fall through
// to the wind tier instead, and there are only 35 such flows in the whole
// global file anyway.
struct TrafficFlow {
    std::string name; // free text authored by the scenery maker; display-only

    // Row 1001, OR'd: the wind condition passes if ANY entry matches. Empty
    // means unrestricted by wind.
    //
    // A vector rather than a single rule because this is load-bearing and easy
    // to get wrong: 403 of the 4,994 flows in X-Plane 12's global apt.dat carry
    // more than one, and the extra entry is nearly always a calm-wind clause
    // listed FIRST (KSEA's flow is literally named "Calm and South flow";
    // LEMD "North" and LEPA "ATC West Flow" have the same shape). Keeping only
    // the last row parsed silently drops the calm case, which then falls
    // through to whatever unconditional fallback flow the airport defines --
    // at LEPA in dead calm that is the difference between 24L/24R (correct)
    // and 06L/06R.
    std::vector<FlowWindRule> wind_rules;

    // Rows 1002/1003. 0 is the file's idiom for unrestricted, and is by far the
    // common case, so these are plain values rather than optionals: "no rule"
    // and "a rule of 0" are indistinguishable in the data and mean the same
    // thing. Non-zero minima do exist (1,351 ceiling and 1,372 visibility rows
    // globally) -- KJFK's CAT I flows require 200ft/0.5sm, which is what makes
    // its "LOW VIS" flows reachable at all.
    double min_ceiling_ft = 0.0;
    double min_visibility_sm = 0.0;

    // Row 1004. nullopt means unrestricted by time, which is the common case
    // (685 rows globally, and 569 of those are not all-day).
    std::optional<FlowTimeRule> time_rule;

    // Rows 1100/1110, in file order. That order is part of the algorithm: the
    // first usable rule wins, so preserving it is correctness, not style.
    std::vector<FlowRunwayUseRule> runway_use_rules;
};

} // namespace trm::core
