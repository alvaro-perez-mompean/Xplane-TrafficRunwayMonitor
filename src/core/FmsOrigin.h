#pragma once

#include <optional>
#include <string>

// Pure decision logic behind FMS origin/destination lookup: the
// native-FMS-entries decision logic, plus the pinned-override merge rule.
// Pulled out of sdk/FmsOrigin specifically because this is pure logic
// (plain strings/values in, values out) with zero XPLM dependency, so it
// stays unit-testable even though sdk/FmsOrigin itself (real dataref
// reads) isn't.

namespace trm::core {

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

// Pinned origin/destination (see
// notes/features/manual-origin-destination-override.md): the effective
// ICAO for one field this cycle is `sourceIcao` whenever `fresh` (the
// source wins outright), else `overrideIcao` -- the last-known value
// (either a previous fresh source read, or a manual ICAO the user typed
// over it) sticky across staleness, nullopt only if the field has never
// resolved anything and the user hasn't typed a value either. Pure merge
// only -- the caller (Plugin.cpp) owns the override's lifecycle: keeping
// it mirroring `sourceIcao` while fresh, and clearing it (only) on an
// actual new-flight signal.
std::optional<std::string> ResolveEffectiveIcao(bool fresh, const std::optional<std::string>& sourceIcao,
                                                 const std::optional<std::string>& overrideIcao);

} // namespace trm::core
