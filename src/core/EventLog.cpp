#include "core/EventLog.h"

#include <utility>

namespace trm::core {

void EventLog::Record(RunwayEvent event)
{
    events_.push_front(std::move(event));
}

void EventLog::PruneOlderThan(double nowSec, double maxAgeSec)
{
    while (!events_.empty() && (nowSec - events_.back().time_sec) > maxAgeSec) {
        events_.pop_back();
    }
}

std::vector<RunwayEventSummary> EventLog::Summaries(double nowSec) const
{
    std::vector<RunwayEventSummary> summaries;
    summaries.reserve(events_.size());
    for (const auto& event : events_) {
        summaries.push_back(RunwayEventSummary{event.icao, event.category, event.runway_id, nowSec - event.time_sec,
                                                event.callsign});
    }
    return summaries;
}

} // namespace trm::core
