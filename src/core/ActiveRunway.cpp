#include "core/ActiveRunway.h"
#include "core/GeoMath.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace trm::core {

namespace {

double Normalize360(double deg)
{
    const double wrapped = std::fmod(deg, 360.0);
    return wrapped < 0.0 ? wrapped + 360.0 : wrapped;
}

// Compass sectors in apt.dat wrap through 360/0 (EGKK's two flows split the
// compass as 348->167 and 168->349), so an ordinary min<=x<=max test is wrong
// for half the data.
//
// The full-circle guard is not defensive: "000 360" is the file's own idiom for
// "any direction", and it is what the leading calm-wind clause almost always
// uses (LEMD "1001 LEMD 000 360 4", LEPA "1001 LEPA 000 360 10"). Normalizing
// 360 to 0 first would collapse that to a zero-width sector matching only a
// wind from exactly north, quietly discarding the calm case these rules exist
// to express.
bool InWrappingSector(double valueDeg, double minDeg, double maxDeg)
{
    if (maxDeg - minDeg >= 360.0) {
        return true;
    }
    const double value = Normalize360(valueDeg);
    const double low = Normalize360(minDeg);
    const double high = Normalize360(maxDeg);
    if (low <= high) {
        return value >= low && value <= high;
    }
    return value >= low || value <= high;
}

// Row-1004 windows wrap through midnight the same way (2000 -> 0500 is a real
// row in the file).
bool InWrappingTimeWindow(int nowMinutes, int startMinutes, int endMinutes)
{
    if (startMinutes <= endMinutes) {
        return nowMinutes >= startMinutes && nowMinutes <= endMinutes;
    }
    return nowMinutes >= startMinutes || nowMinutes <= endMinutes;
}

// Wind rules within one flow are OR'd; the rule categories are AND'd. A missing
// category is unrestricted, not failing -- 390 flows carry no wind rule at all
// and exist precisely to be unconditional fallbacks.
bool FlowRulesPass(const TrafficFlow& flow, const ActiveRunwayConditions& conditions)
{
    if (!flow.wind_rules.empty()) {
        const bool anyWindRuleMatches =
            std::any_of(flow.wind_rules.begin(), flow.wind_rules.end(), [&](const FlowWindRule& rule) {
                return InWrappingSector(conditions.wind_from_true_deg, rule.dir_min_deg, rule.dir_max_deg) &&
                       conditions.wind_speed_kt <= rule.max_speed_kt;
            });
        if (!anyWindRuleMatches) {
            return false;
        }
    }
    // 0 is apt.dat's idiom for unrestricted, not a minimum of zero.
    if (flow.min_ceiling_ft > 0.0 && conditions.ceiling_ft < flow.min_ceiling_ft) {
        return false;
    }
    if (flow.min_visibility_sm > 0.0 && conditions.visibility_sm < flow.min_visibility_sm) {
        return false;
    }
    if (flow.time_rule &&
        !InWrappingTimeWindow(conditions.utc_minutes, flow.time_rule->start_utc_minutes,
                              flow.time_rule->end_utc_minutes)) {
        return false;
    }
    return true;
}

bool RuleCoversOperation(const FlowRunwayUseRule& rule, RunwayOperation operation)
{
    return operation == RunwayOperation::kArrival ? rule.arrivals : rule.departures;
}

bool RuleCoversAircraftClass(const FlowRunwayUseRule& rule, const std::string& aircraftClass)
{
    if (aircraftClass.empty()) {
        return true; // caller has no specific aircraft in mind -- see ActiveRunwayConditions
    }
    return std::find(rule.aircraft_classes.begin(), rule.aircraft_classes.end(), aircraftClass) !=
           rule.aircraft_classes.end();
}

// apt.dat is not internally consistent about leading zeros in runway
// designators: KJFK spells its ends "4L"/"4R" on row 100 but "04L"/"04R" in its
// own flow rules. 837 of the 7,256 runway-in-use rows in X-Plane 12's global
// apt.dat name an id that appears on no row 100 at that airport, and stripping
// leading zeros resolves 820 of them. Comparing the two spellings literally
// silently drops flow selection at KJFK, PANC, PABE and friends.
std::string NormalizeRunwayId(const std::string& id)
{
    std::size_t firstKept = 0;
    while (firstKept + 1 < id.size() && id[firstKept] == '0') {
        ++firstKept;
    }
    return id.substr(firstKept);
}

// The airport's OWN spelling of the runway a flow rule names, or nullopt if it
// has no such runway. Returning the airport's spelling rather than the rule's
// matters: everything downstream (runway length lookup, sighting keys, the
// dashboard) is keyed on what row 100 called it.
//
// The 17 rules that survive normalization unmatched are stale data naming
// runways the airport no longer has (KMDW 13C/31C, LFAB 13S/31S). Skipping
// them is correct.
std::optional<std::string> ResolveRunwayId(const Airport& airport, const std::string& ruleRunwayId)
{
    const std::string normalized = NormalizeRunwayId(ruleRunwayId);
    for (const RunwayEnd& rwyEnd : airport.runways) {
        if (rwyEnd.id == ruleRunwayId || NormalizeRunwayId(rwyEnd.id) == normalized) {
            return rwyEnd.id;
        }
    }
    return std::nullopt;
}

std::optional<ActiveRunwayResult> SelectViaFlows(const Airport& airport, const ActiveRunwayConditions& conditions,
                                                  RunwayOperation operation)
{
    for (const TrafficFlow& flow : airport.flows) {
        if (!FlowRulesPass(flow, conditions)) {
            continue;
        }

        for (const FlowRunwayUseRule& rule : flow.runway_use_rules) {
            if (!RuleCoversOperation(rule, operation) || !RuleCoversAircraftClass(rule, conditions.aircraft_class)) {
                continue;
            }
            // A rule naming a runway this airport doesn't have is stale data;
            // skip it rather than report a runway nothing downstream can look
            // up a length or geometry for.
            const std::optional<std::string> runwayId = ResolveRunwayId(airport, rule.runway_id);
            if (!runwayId) {
                continue;
            }
            return ActiveRunwayResult{*runwayId, ActiveRunwaySource::kSimFlow, flow.name};
        }

        // This flow's environmental rules passed but none of its runway-in-use
        // rules cover this operation (or this aircraft class). Fall through to
        // the wind tier rather than trying the NEXT flow: flow order encodes
        // weather and time priority, not an operation/class fallback chain, so
        // advancing would pick a flow whose conditions demonstrably do not hold
        // right now.
        return std::nullopt;
    }
    return std::nullopt;
}

struct CrosswindCandidate {
    std::string runway_id;
    double headwind_kt = 0.0;
    double crosswind_kt = 0.0;
};

// Lowest runway id, as a last-resort deterministic tie-break. Parallel ends
// share a heading exactly (LEMD's 36L and 36R), so without this the winner
// would be decided by apt.dat row order, which carries no information.
const CrosswindCandidate& LowestIdCandidate(const std::vector<CrosswindCandidate>& candidates)
{
    return *std::min_element(candidates.begin(), candidates.end(),
                             [](const CrosswindCandidate& a, const CrosswindCandidate& b) {
                                 return a.runway_id < b.runway_id;
                             });
}

} // namespace

std::optional<std::string> SelectCrosswindFavoredRunway(const Airport& airport, double windFromTrueDeg,
                                                         double windSpeedKt, const ActiveRunwayConfig& config)
{
    if (airport.runways.empty()) {
        return std::nullopt;
    }

    std::vector<CrosswindCandidate> candidates;
    candidates.reserve(airport.runways.size());
    for (const RunwayEnd& rwyEnd : airport.runways) {
        // RunwayEnd::heading_deg is already the direction of travel when using
        // this end (bearing from this threshold toward the other), for both
        // landing and departing -- see ParseAptDat. So a wind blowing FROM that
        // same direction is a pure headwind.
        const double offsetDeg = AngleDiffDeg(windFromTrueDeg, rwyEnd.heading_deg);
        const double offsetRad = ToRadians(offsetDeg);
        candidates.push_back(CrosswindCandidate{rwyEnd.id, windSpeedKt * std::cos(offsetRad),
                                                 std::abs(windSpeedKt * std::sin(offsetRad))});
    }

    if (windSpeedKt < config.calm_wind_kt) {
        return LowestIdCandidate(candidates).runway_id;
    }

    std::vector<CrosswindCandidate> usable;
    for (const CrosswindCandidate& candidate : candidates) {
        if (candidate.headwind_kt < -config.tailwind_tolerance_kt) {
            continue;
        }
        usable.push_back(candidate);
    }
    if (usable.empty()) {
        // Can't happen with well-formed reciprocal pairs (one end of any pair
        // always has a headwind), but a malformed single-ended runway could.
        return LowestIdCandidate(candidates).runway_id;
    }

    std::stable_sort(usable.begin(), usable.end(), [](const CrosswindCandidate& a, const CrosswindCandidate& b) {
        if (a.crosswind_kt != b.crosswind_kt) {
            return a.crosswind_kt < b.crosswind_kt;
        }
        if (a.headwind_kt != b.headwind_kt) {
            return a.headwind_kt > b.headwind_kt;
        }
        return a.runway_id < b.runway_id;
    });
    return usable.front().runway_id;
}

std::optional<ActiveRunwayResult> SelectActiveRunway(const Airport& airport,
                                                      const ActiveRunwayConditions& conditions,
                                                      RunwayOperation operation, const ActiveRunwayConfig& config)
{
    if (airport.runways.empty()) {
        return std::nullopt;
    }

    if (std::optional<ActiveRunwayResult> viaFlow = SelectViaFlows(airport, conditions, operation)) {
        return viaFlow;
    }

    const std::optional<std::string> viaCrosswind =
        SelectCrosswindFavoredRunway(airport, conditions.wind_from_true_deg, conditions.wind_speed_kt, config);
    if (!viaCrosswind) {
        return std::nullopt;
    }
    return ActiveRunwayResult{*viaCrosswind, ActiveRunwaySource::kCrosswind, std::string{}};
}

std::string ActiveRunwaySourceLabel(ActiveRunwaySource source)
{
    switch (source) {
        case ActiveRunwaySource::kSimFlow:
            return "X-Plane's own ATC flow rules";
        case ActiveRunwaySource::kCrosswind:
            return "wind direction only";
    }
    return "an estimate";
}

} // namespace trm::core
