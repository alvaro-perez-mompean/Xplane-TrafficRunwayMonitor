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
    // Stamped only when ParseToLissInitPageFromTo actually succeeds -- NOT
    // on every call/cycle -- so IsFresh() below can tell "still parsing a
    // live FROM/TO line" apart from "holding a latched value while the
    // MCDU shows something else" (or a different, non-ToLiss aircraft
    // entirely). nullopt until the first successful parse.
    std::optional<double> last_confirmed_at_sec;
};

// Updates `state` in place with a fresh confirmed FROM/TO if `snapshot`
// parses (ParseToLissInitPageFromTo), stamping last_confirmed_at_sec to
// `nowSec`; otherwise leaves `state` untouched -- holds the last confirmed
// value rather than clearing it, since the MCDU mirrors whatever page
// happens to be on screen and a possibly-stale-but-real value is judged
// better than flickering to "unknown" every time the page changes
// elsewhere. Staleness is left to IsFresh() against last_confirmed_at_sec
// instead.
void UpdateToLissFmsState(ToLissFmsState& state, const ToLissMcduSnapshot& snapshot, double nowSec);

// Threshold (seconds) past which a source's last confirmed value is
// considered stale -- see IsFresh().
constexpr double kFmsFreshnessThresholdSec = 5.0;

// True if `lastConfirmedAtSec` is set and within `thresholdSec` of `nowSec`.
// False if `lastConfirmedAtSec` is nullopt (never confirmed at all).
bool IsFresh(std::optional<double> lastConfirmedAtSec, double nowSec, double thresholdSec);

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

// Manual origin/destination override (see
// notes/features/manual-origin-destination-override.md): the effective
// ICAO for one field this cycle is `sourceIcao` whenever `fresh` (the
// source wins outright, ignoring any standing override), else
// `overrideIcao` (nullopt if the user hasn't typed one for this field
// yet). Pure merge only -- the caller (Plugin.cpp) owns clearing the
// override once `fresh` is true again, since that's a stateful side
// effect this function has no state to perform.
std::optional<std::string> ResolveEffectiveIcao(bool fresh, const std::optional<std::string>& sourceIcao,
                                                 const std::optional<std::string>& overrideIcao);

} // namespace trm::core
