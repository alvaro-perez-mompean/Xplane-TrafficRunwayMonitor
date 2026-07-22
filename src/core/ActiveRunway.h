#pragma once

#include <optional>
#include <string>

#include "core/AptDat.h"

// Active-runway selection: which runway is in use for arrivals, and which for
// departures, when no traffic-confirmed sighting exists yet.
//
// Two tiers, in order:
//
//   1. Replicate X-Plane's own authored ATC Flow evaluation (apt.dat rows
//      1000-1004 and 1100/1110, see core/TrafficFlow.h). Flows are evaluated
//      strictly in file order and the FIRST flow whose rules all pass is
//      selected -- not scored, not best-match. Its runway-in-use rules are then
//      filtered by operation (and aircraft class, if the caller knows one), and
//      the first survivor in file order wins.
//   2. A wind/crosswind heuristic, used when the airport has no flows at all
//      (~96% of airports), when no flow's rules pass, or when the flow that did
//      pass has no runway-in-use rule covering this operation (rare: 17 flows
//      in the global file cover no arrivals and 18 cover no departures, out of
//      4,994, e.g. LILI's "27" flow and KMMH's two "RWY .. DEP" flows).
//
// Tier 1 exists because there is no way to read the sim's answer back. Every
// sim/atc/* dataref is COM radio state; the XPLM SDK has no runway-in-use API
// at all. Consuming the same authored data X-Plane reads is the closest we can
// get to agreeing with it.
//
// Pure logic, no XPLM calls: weather and time come in as plain values already
// resolved by the caller (sdk/Weather, and sim/time/zulu_time_sec read in
// Plugin.cpp).

namespace trm::core {

enum class RunwayOperation { kArrival, kDeparture };

// Which tier produced an answer, for UI labelling. kSimFlow is a materially
// stronger claim than kCrosswind: it is what X-Plane's own ATC would do, named
// rule and all, rather than our guess at what a controller would prefer.
enum class ActiveRunwaySource { kSimFlow, kCrosswind };

struct ActiveRunwayConditions {
    double wind_from_true_deg = 0.0; // wind FROM direction, true
    double wind_speed_kt = 0.0;
    double ceiling_ft = kUnrestrictedCeilingFt;
    double visibility_sm = kUnrestrictedVisibilitySm;
    // Minutes since 0000Z, [0, 1440). Only consulted by flows that carry a row
    // 1004; most do not.
    int utc_minutes = 0;
    // apt.dat's own class string ("heavy", "jets", "turboprops", "props",
    // "helos", "fighters"), or empty for "any class".
    //
    // Empty is the right default for this plugin and the important difference
    // from a traffic-generating consumer of the same data: we are predicting
    // which runway the NEXT unknown aircraft will use, not routing a specific
    // one we just spawned. With a class named, a rule must list it; with none,
    // any rule covering the operation is accepted.
    std::string aircraft_class;
};

struct ActiveRunwayConfig {
    // Below this, wind direction is noise -- skip the crosswind comparison and
    // go straight to the deterministic tie-break rather than let a 1kt puff
    // pick a runway. Real calm-wind runway preferences are usually
    // noise-abatement driven and apt.dat carries them in no usable form.
    double calm_wind_kt = 3.0;
    // Runways can legally take a small tailwind; beyond this an end is rejected
    // outright. Both constants are inherited placeholders, not aeronautically
    // researched.
    double tailwind_tolerance_kt = 5.0;
};

struct ActiveRunwayResult {
    std::string runway_id;
    ActiveRunwaySource source = ActiveRunwaySource::kCrosswind;
    // The matched row-1000 name, empty unless source is kSimFlow. Free text
    // authored by whoever built the scenery ("Westerly Flow south landing",
    // "22L LOW VIS"), so treat as display-only and bound its length when shown.
    std::string flow_name;
};

// nullopt only when the airport has no runways at all.
std::optional<ActiveRunwayResult> SelectActiveRunway(const Airport& airport,
                                                      const ActiveRunwayConditions& conditions,
                                                      RunwayOperation operation,
                                                      const ActiveRunwayConfig& config = {});

// Tier 2 on its own, exposed for the callers that only have wind and no reason
// to consult flows (and for direct testing). Same tie-break chain: reject
// unacceptable tailwind, then minimum crosswind, then maximum headwind, then
// lowest runway id.
std::optional<std::string> SelectCrosswindFavoredRunway(const Airport& airport, double windFromTrueDeg,
                                                         double windSpeedKt,
                                                         const ActiveRunwayConfig& config = {});

// Human-readable label for a tier, for UI tooltips.
std::string ActiveRunwaySourceLabel(ActiveRunwaySource source);

} // namespace trm::core
