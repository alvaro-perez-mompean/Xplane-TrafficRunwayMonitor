#include "core/SightingTracker.h"

#include <array>

namespace trm::core {

SightingTracker::SightingTracker(SightingConfig config)
    : config_(config)
{
}

std::optional<RunwayEvent> SightingTracker::ProcessSlot(int slotIndex, SlotSightingState& slotState,
                                                          const SlotObservation& observation, double nowSec)
{
    if (observation.icao.empty() || observation.runway_id.empty()) {
        return std::nullopt;
    }

    const std::string& icao = observation.icao;
    const std::string& runwayId = observation.runway_id;
    const FlightPhase phase = observation.phase;

    std::optional<RunwayEvent> confirmedEvent;

    // Refreshed whenever this aircraft's phase is taxi, takeoff_roll, OR
    // landing_rollout -- independent of the phase-specific action below,
    // not mutually exclusive with it (a landing_rollout cycle both
    // refreshes this AND may confirm a pending arrival).
    if (phase == FlightPhase::kTaxi || phase == FlightPhase::kTakeoffRoll || phase == FlightPhase::kLandingRollout) {
        slotState.ground_sighting = SightingMark{icao, runwayId, nowSec};
    }

    if (phase == FlightPhase::kInitialClimb) {
        // takeoff_roll alone never records anything -- only refreshes
        // ground_sighting above. A departure is only recorded once this
        // same aircraft is subsequently seen airborne and climbing here,
        // aligned with the same runway, within the confirm window.
        const auto& ground = slotState.ground_sighting;
        if (ground && ground->icao == icao && ground->runway_id == runwayId
            && (nowSec - ground->time_sec) <= config_.departure_confirm_window_sec) {
            if (RecordSighting(icao, SightingCategory::kDeparture, runwayId, observation.other_end_id, slotIndex,
                                nowSec)) {
                confirmedEvent = RunwayEvent{icao, SightingCategory::kDeparture, runwayId, nowSec, observation.callsign};
            }
        }
    } else if (phase == FlightPhase::kFinalApproach) {
        // Never records directly -- only builds up (and, once seen for
        // config_.final_approach_confirm_cycles consecutive cycles, sets)
        // state for a subsequent touchdown to confirm.
        if (slotState.pending_arrival_candidate_icao == icao
            && slotState.pending_arrival_candidate_runway_id == runwayId) {
            ++slotState.pending_arrival_candidate_streak;
        } else {
            slotState.pending_arrival_candidate_icao = icao;
            slotState.pending_arrival_candidate_runway_id = runwayId;
            slotState.pending_arrival_candidate_streak = 1;
        }
        if (slotState.pending_arrival_candidate_streak >= config_.final_approach_confirm_cycles) {
            slotState.pending_arrival = SightingMark{icao, runwayId, nowSec};
        }
    } else if (phase == FlightPhase::kTaxi || phase == FlightPhase::kLandingRollout) {
        const auto& pending = slotState.pending_arrival;
        if (pending && pending->icao == icao && pending->runway_id == runwayId
            && (nowSec - pending->time_sec) <= config_.arrival_confirm_window_sec) {
            if (RecordSighting(icao, SightingCategory::kArrival, runwayId, observation.other_end_id, slotIndex,
                                nowSec)) {
                confirmedEvent = RunwayEvent{icao, SightingCategory::kArrival, runwayId, nowSec, observation.callsign};
            }
        }
        // Cleared whether or not it matched: a go-around/missed approach
        // just lets a stale pending mark expire unconfirmed.
        slotState.pending_arrival.reset();
    }

    return confirmedEvent;
}

void SightingTracker::ClearSlotState(SlotSightingState& slotState) const
{
    slotState.ground_sighting.reset();
    slotState.pending_arrival.reset();
    slotState.pending_arrival_candidate_icao.clear();
    slotState.pending_arrival_candidate_runway_id.clear();
    slotState.pending_arrival_candidate_streak = 0;
}

void SightingTracker::PruneStaleSightings(double nowSec, double maxAgeSec)
{
    for (auto& [icao, airportSightings] : sightings_) {
        for (RunwaySightings* categoryMap : std::array<RunwaySightings*, 2>{&airportSightings.arrival, &airportSightings.departure}) {
            for (auto runwayIt = categoryMap->begin(); runwayIt != categoryMap->end();) {
                ContributorMap& contributors = runwayIt->second;
                for (auto contribIt = contributors.begin(); contribIt != contributors.end();) {
                    if (nowSec - contribIt->second > maxAgeSec) {
                        contribIt = contributors.erase(contribIt);
                    } else {
                        ++contribIt;
                    }
                }
                if (contributors.empty()) {
                    runwayIt = categoryMap->erase(runwayIt);
                } else {
                    ++runwayIt;
                }
            }
        }
    }
}

const RunwaySightings* SightingTracker::FindSightings(const std::string& icao, SightingCategory category) const
{
    const auto it = sightings_.find(icao);
    if (it == sightings_.end()) {
        return nullptr;
    }
    return (category == SightingCategory::kArrival) ? &it->second.arrival : &it->second.departure;
}

RunwaySightings& SightingTracker::CategoryMapFor(const std::string& icao, SightingCategory category)
{
    AirportSightings& airportSightings = sightings_[icao]; // auto-vivify: write path only
    return (category == SightingCategory::kArrival) ? airportSightings.arrival : airportSightings.departure;
}

bool SightingTracker::RecordSighting(const std::string& icao, SightingCategory category, const std::string& runwayId,
                                      const std::string& otherEndId, int slotIndex, double nowSec)
{
    ContributorMap& contributors = CategoryMapFor(icao, category)[runwayId];
    const bool isNewContributor = contributors.find(slotIndex) == contributors.end();
    contributors[slotIndex] = nowSec;

    if (!otherEndId.empty()) {
        InvalidateRunwayEnd(icao, otherEndId);
    }
    return isNewContributor;
}

void SightingTracker::InvalidateRunwayEnd(const std::string& icao, const std::string& runwayId)
{
    const auto airportIt = sightings_.find(icao);
    if (airportIt == sightings_.end()) {
        return;
    }
    AirportSightings& airportSightings = airportIt->second;
    for (RunwaySightings* categoryMap : std::array<RunwaySightings*, 2>{&airportSightings.arrival, &airportSightings.departure}) {
        const auto runwayIt = categoryMap->find(runwayId);
        if (runwayIt != categoryMap->end()) {
            runwayIt->second.clear(); // entry stays present, just emptied
        }
    }
}

} // namespace trm::core
