#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <thread>

#include "core/SimbriefOfp.h"

// Async fetch of a Simbrief OFP (flight plan) via Simbrief's public HTTP
// API, triggered by an explicit user action (Flight Plan tab's "Fetch from
// Simbrief" button) -- never polled automatically. Real network glue, not
// unit-tested -- see core/SimbriefOfp.h for the pure JSON-parsing half.
//
// Windows-only real implementation (WinINet) -- this plugin is only ever
// deployed on Windows (see CLAUDE.md's Deploy section). On other platforms
// RequestFetch() synchronously reports "not supported" so the rest of this
// codebase (and CI's Mac/Linux release builds) doesn't need a working
// cross-platform HTTP stack for a feature that's Windows-only anyway.

namespace trm::sdk {

// Mutex-guarded fetch result (+ on Windows, the live WinINet handles, so
// ~SimbriefClient() can close them to unstick a worker thread blocked on
// network I/O -- see SimbriefClient.cpp's own comment). File-scope rather
// than a member of SimbriefClient so SimbriefClient.cpp's free-function
// worker (RunFetch) can access it -- it's captured by the worker thread
// *by value* via shared_ptr, never via raw `this`, so the worker only ever
// touches this shared state, never the SimbriefClient object itself, and
// stays safe even if SimbriefClient's own destructor is mid-flight.
// Definition is private to SimbriefClient.cpp; only forward-declared here.
struct SimbriefClientSharedState;

enum class SimbriefFetchStatus { kIdle, kFetching, kSuccess, kError };

struct SimbriefFetchResult {
    SimbriefFetchStatus status = SimbriefFetchStatus::kIdle;
    std::optional<std::string> origin_icao;
    std::optional<std::string> destination_icao;
    // LIDO-style route line -- see core::SimbriefOriginDestination::route_text.
    std::optional<std::string> route_text;
    // Fuel figures -- see core::SimbriefFuelPlan. Default-constructed (all
    // fields nullopt) until the first successful fetch.
    core::SimbriefFuelPlan fuel;
    // Weight figures -- see core::SimbriefWeights. Same default-constructed-
    // until-first-fetch convention as fuel above.
    core::SimbriefWeights weights;
    // Header/identity figures -- see core::SimbriefHeader. Same default-
    // constructed-until-first-fetch convention as fuel/weights above.
    core::SimbriefHeader header;
    std::string error_message; // meaningful only when status == kError
    // Bumped once per completed fetch (success or error) -- lets Plugin.cpp
    // apply a just-finished fetch's ICAOs into the origin/destination
    // override exactly once, instead of re-stamping the same cached result
    // over the user's later edits on every subsequent ~1Hz Poll().
    std::uint64_t generation = 0;
};

class SimbriefClient {
public:
    SimbriefClient();
    ~SimbriefClient();
    SimbriefClient(const SimbriefClient&) = delete;
    SimbriefClient& operator=(const SimbriefClient&) = delete;

    // Starts an async fetch for `pilotId` on a background thread; no-op if
    // a fetch is already in flight. Never blocks the caller -- Plugin.cpp's
    // RunAnalysisCycle (~1Hz flight loop callback) must never block on
    // network I/O.
    void RequestFetch(const std::string& pilotId);

    // Cheap: mutex lock + struct copy, no I/O. Safe to call every cycle.
    SimbriefFetchResult Poll() const;

private:
    std::shared_ptr<SimbriefClientSharedState> state_;
    std::thread worker_;
};

} // namespace trm::sdk
