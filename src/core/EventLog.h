#pragma once

#include <deque>
#include <string>
#include <vector>

#include "core/SightingTracker.h"

// Chronological rolling log of confirmed runway sightings, feeding the UI's
// History tab. Deliberately minimal,
// mirroring core::SightingTracker's own "just accumulate, let the caller
// query/prune every cycle" shape rather than duplicating any of that
// module's confirm-before/confirm-after logic.

namespace trm::core {

// One History-tab row, resolved fresh each cycle by Summaries(nowSec) --
// elapsed_sec mirrors core::RunwaySightingSummary's own "seconds relative to
// nowSec" convention (Aggregator.h) so the UI can format it with the same
// core::FormatAgo already used everywhere else, without needing to know the
// current time itself.
struct RunwayEventSummary {
    std::string icao;
    SightingCategory category;
    std::string runway_id;
    double elapsed_sec = 0.0;
    std::string callsign; // empty when the source that confirmed this sighting carries no aircraft identity
};

class EventLog {
public:
    void Record(RunwayEvent event);

    // Drops events older than maxAgeSec relative to nowSec. Caller passes a
    // fresh maxAgeSec every cycle (e.g. 2x the user-adjustable active
    // window), same "read fresh every cycle" convention as
    // SightingTracker::PruneStaleSightings.
    void PruneOlderThan(double nowSec, double maxAgeSec);

    // Most-recent-first summaries for direct display. Callers should call
    // PruneOlderThan first if they want a bounded result.
    std::vector<RunwayEventSummary> Summaries(double nowSec) const;

private:
    std::deque<RunwayEvent> events_; // most-recent-first: Record pushes front, PruneOlderThan pops back
};

} // namespace trm::core
