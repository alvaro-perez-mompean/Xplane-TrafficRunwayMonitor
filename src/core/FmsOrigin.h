#pragma once

#include <optional>
#include <string>
#include <utility>

// Pure decision logic behind FMS origin/destination lookup: the ToLiss
// MCDU1 FROM/TO line parser + latching state, and the native-FMS-entries
// decision logic. Pulled out of sdk/FmsOrigin specifically because both are
// pure logic (plain strings/values in, values out) with zero XPLM
// dependency, so they stay unit-testable even though sdk/FmsOrigin itself
// (real dataref reads) isn't.

namespace trm::core {

// One already-read MCDU1 screen-mirror snapshot.
struct ToLissMcduSnapshot {
    std::string title;
    std::string label1;
    std::string cont1b;
};

// Parses the FROM/TO line pattern (two 4-letter ICAO codes separated by
// "/") out of an MCDU1 INIT page snapshot -- confirms BOTH the title
// contains "INIT" AND label1 contains "FROM/TO" before trusting cont1b's
// content, guarding against a future ToLiss build reordering INIT page
// lines while still calling the page "INIT". Returns {origin, destination}
// if it matches, nullopt if the snapshot isn't (confirmed to be) the INIT
// page's FROM/TO line, or the line doesn't match the two-ICAO-codes
// pattern.
std::optional<std::pair<std::string, std::string>> ParseToLissInitPageFromTo(const ToLissMcduSnapshot& snapshot);

// Persists the last confirmed FROM/TO across calls/cycles.
struct ToLissFmsState {
    std::optional<std::string> last_confirmed_origin;
    std::optional<std::string> last_confirmed_destination;
};

// Updates `state` in place with a fresh confirmed FROM/TO if `snapshot`
// parses (ParseToLissInitPageFromTo); otherwise leaves it untouched --
// holds the last confirmed value rather than clearing it, since the MCDU
// mirrors whatever page happens to be on screen and a possibly-stale-but-
// real value is judged better than flickering to "unknown" every time the
// page changes elsewhere.
void UpdateToLissFmsState(ToLissFmsState& state, const ToLissMcduSnapshot& snapshot);

// One native FMS entry's relevant fields (XPLMGetFMSEntryInfo's
// type/id out-params, already fetched by the caller).
struct FmsEntryInfo {
    bool is_airport = false;
    std::string id;
};

struct NativeFmsOriginDestination {
    std::optional<std::string> origin_icao;
    std::optional<std::string> destination_icao;
};

// Native-FMS path: origin is entry 0, destination is the *last* entry (entryCount - 1) --
// deliberately not XPLMGetDestinationFMSEntry(), which returns the current
// active leg, not the destination (a real, documented X-Plane SDK quirk
// this works around -- see sdk::FmsOrigin::Resolve's own comment).
// `entryCount <= 0` (no flight plan programmed) yields both nullopt.
NativeFmsOriginDestination ResolveNativeFmsOriginDestination(int entryCount, const FmsEntryInfo& originEntry,
                                                               const FmsEntryInfo& destinationEntry);

} // namespace trm::core
