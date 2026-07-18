#pragma once

#include <cstdio>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"

#include "core/AdvisoryFormat.h"
#include "core/Aggregator.h"
#include "core/AptDat.h"
#include "core/EventLog.h"
#include "core/Format.h"
#include "core/SimbriefOfp.h"
#include "core/WindEstimate.h"
#include "ui/Theme.h"

// Reusable ImGui render helpers operating on core:: display data,
// including the full-dashboard widgets (runway diagram, chip selector,
// event history table).
//
// Thin ImGui glue, not unit-tested; every function here only reads
// core:: data, it never computes it.

namespace trm::ui {

// Opaque handle returned by BeginCard(), passed back to EndCard(). Callers
// shouldn't inspect its fields.
struct CardScope {
    ImVec2 start_pos;
    float content_width = 0.0f;
};

// Opens a themed "card" panel (background + border) around whatever widgets
// the caller submits before the matching EndCard(). Uses
// ImDrawList::ChannelsSplit rather than ImGui::BeginChild, since this ImGui
// version's BeginChild can't auto-size its height to content.
CardScope BeginCard();
void EndCard(const CardScope& scope);

// Which of the three confidence tiers a specific runway end currently
// falls into, for the compass diagram and any other color-coded display:
// kColorConfirmed if `runwayId` appears in either category's active list,
// kColorWindEstimate if it's the wind-estimate pick and at least one
// category needs it, else kColorWaiting (covers history picks and
// anything with no data at all).
ImU32 RunwayStatusColor(const core::AirportEntry& entry, const std::string& runwayId);

// Dimmed "(NNNN ft)" suffix on the current line, no-op if lengthFt is
// nullopt.
void RenderLengthSuffix(const std::optional<double>& lengthFt);

// One category's (Departures or Arrivals) lines: active (green, one per
// runway) / history (dim gray) / wind estimate (amber, hoverable "(?)")
// / waiting (dim gray).
void RenderCategorySection(const char* title, const core::CategoryResult& category,
                            const std::optional<core::WindEstimateResult>& windEstimate);

// Word-wrapped render of a precomputed advisory sentence (core::
// ResolveAdvisoryText, resolved once per orchestration cycle in
// Plugin.cpp -- this function does no core:: computation itself, per
// CLAUDE.md's ui/ layering rule), e.g. "Currently landing and departing
// runway 31, wind 310 at 8, QNH 1013." Picks
// advisoryText.without_wind_and_altimeter when includeWindAndAltimeter is
// false (the caller already shows that data elsewhere -- RenderAirportCard's
// Both mode), else .with_wind_and_altimeter.
void RenderAdvisorySentence(const core::ResolvedAdvisoryText& advisoryText, bool includeWindAndAltimeter = true);

// One airport's full card: header (ICAO + distance), altimeter setting
// (formatted per `pressureUnit`, Settings tab), then -- per `displayMode`
// (Settings tab) -- the natural-language sentence and/or the classic
// Departures/Arrivals bullet lines, optional raw-METAR debug line.
// `advisoryText` is the precomputed core::ResolveAdvisoryText result for
// `entry` (Plugin.cpp's orchestration cycle resolves it alongside entry
// itself); nullopt skips the sentence even if displayMode calls for it
// (defensive only -- callers always have both resolved together). Set
// showHeader=false when the ICAO/name/distance is already shown by a
// caller above (e.g. the nearby-airport combo box's own preview), to
// avoid repeating it.
void RenderAirportCard(const core::AirportEntry& entry, bool showRawMetar, core::PressureUnit pressureUnit,
                        core::AdvisoryDisplayMode displayMode,
                        const std::optional<core::ResolvedAdvisoryText>& advisoryText, bool showHeader = true);

// Centerpiece widget: a north-up, to-scale plan view of every runway,
// projected from each
// threshold's lat/lon onto a local tangent plane (core::LocalOffsetFromReference)
// and scaled to fit `diameter`. Real relative position, spacing, and length
// are preserved, so parallel runways (09L/09R/09C etc.) render at their true
// lateral offset instead of overlapping. Each runway end is colored via
// RunwayStatusColor. `entry` is nullable -- draws an uncolored (all-waiting)
// diagram if no traffic data is available yet for this airport.
void RenderRunwayDiagram(const core::Airport* airport, const core::AirportEntry* entry,
                          float diameter = kRunwayDiagramDiameter);

// History tab widget: a table
// of confirmed departures/arrivals, most-recent-first, one row per
// core::RunwayEventSummary -- time formatted with the same core::FormatAgo
// used elsewhere, type color-coded like the active/confirmed entries in
// RenderCategorySection. Aircraft is a dim "--" whenever callsign is empty
// (TCAS Override / legacy multiplayer traffic carries no aircraft identity
// at all -- only LTAPI-sourced sightings ever populate it). Renders a dim
// placeholder line instead of an empty table when `events` is empty.
void RenderEventHistory(const std::vector<core::RunwayEventSummary>& events);

// Flight Plan tab: LIDO-style fuel summary table from the last successful
// Simbrief fetch (see core::SimbriefFuelPlan) -- Taxi/Trip/Contingency/
// Alternate/Reserve/Extra rows plus a highlighted Block total, unit
// suffixed in the header when Simbrief reported one (kgs/lbs). Each row is
// only rendered when its own figure is present -- see
// core::SimbriefFuelPlan's own comment on nullopt vs. genuine zero.
// Renders nothing at all if `fuel` has no figures whatsoever (no fetch yet).
void RenderSimbriefFuelPlan(const core::SimbriefFuelPlan& fuel);

// Flight Plan tab: LIDO-style weights summary table from the last
// successful Simbrief fetch (see core::SimbriefWeights) -- PAX (plain
// headcount)/Cargo/Payload/ZFW/Fuel/TOW/LAW rows, EST and MAX columns.
// Unlike a real LIDO OFP, there is no ACTUAL column -- this plugin has no
// live weight source to fill it with. The Fuel row is sourced from `fuel`
// (block/max_tanks) rather than `weights` -- same figures already shown in
// RenderSimbriefFuelPlan's own table, just tonnes-formatted here to match
// the rest of this table -- and gets a "poss extra" note when max_tanks
// exceeds the planned block fuel. Each row (and the whole table) only
// renders when it has at least one figure to show.
void RenderSimbriefWeights(const core::SimbriefWeights& weights, const core::SimbriefFuelPlan& fuel);

// Flight Plan tab: header/identity block from the last successful Simbrief
// fetch (see core::SimbriefHeader) -- callsign/departure date/aircraft
// type+reg/cost index on one line, then ALTN, FL STEPS, AVG W/C + AVG ISA,
// and the OFP's own release id/date, each only shown if present. Doesn't
// repeat origin/destination (already shown by the ICAO override fields
// above it) or TOW/ZFW/LAW (already in RenderSimbriefWeights' table).
// Renders nothing at all if `header` has no figures whatsoever.
void RenderSimbriefHeader(const core::SimbriefHeader& header);

// Caller-owned persistent state for one RenderIcaoOverrideField call site
// -- ImGui is immediate-mode and doesn't own text-editing state itself,
// and a plain function-local static wouldn't distinguish the origin call
// site from the destination one. `buf` is the live ImGui::InputText
// buffer; `last_committed_buf` and `last_seen_reset_epoch` back the two
// mechanisms documented on RenderIcaoOverrideField below.
struct IcaoOverrideFieldState {
    char buf[5] = "";
    char last_committed_buf[5] = "";
    int last_seen_reset_epoch = 0;
    int last_seen_override_epoch = 0;
};

// Flight Plan tab: a 4-char, uppercase ICAO ImGui::InputText. Read-only
// and mirrors `effectiveIcao` every frame while `editable` is false.
// While editable, the buffer is normally left alone for the user to type
// in -- staleness alone does NOT blank it, since Plugin.cpp keeps the
// pinned value sticky across a source going quiet -- except when
// `resetEpoch` or `overrideEpoch` differs from what this call site last saw
// (`state.last_seen_reset_epoch`/`last_seen_override_epoch`), which
// force-mirrors the buffer back to `effectiveIcao` once. `resetEpoch`
// changes when a new flight just cleared the pin; `overrideEpoch` changes
// when something other than the user's own typing set the pin's value
// (e.g. a Simbrief fetch) -- without the latter, a field left editable
// (no fresh native-FMS entry) would never show a value filled in that way,
// since it wasn't typed into this buffer and reaching this field isn't a
// flight reset. Each successful edit calls `onChanged` immediately (same
// synchronous-callback pattern as RenderNearbyAirportSelector's caller).
//
// Also repairs a real ImGui/XPLM interaction: third_party/ImgWindow's
// HandleKeyFuncCB simulates an Escape keypress whenever this plugin window
// loses X-Plane keyboard focus (e.g. clicking into the 3D world instead of
// pressing Enter), and ImGui's InputText treats Escape as "cancel edit",
// silently reverting the buffer to its pre-activation content despite
// still reporting the edit as accepted. `state.last_committed_buf`
// recovers the actually-typed value in that case.
//
// A one-line dim status explaining why the field is locked/editable, plus
// `airportName` (nullopt renders as an "unknown ICAO" warning) render
// underneath.
void RenderIcaoOverrideField(const char* label, IcaoOverrideFieldState& state, bool editable, int resetEpoch,
                              int overrideEpoch, const std::optional<std::string>& effectiveIcao,
                              const std::optional<std::string>& airportName,
                              const std::function<void(const std::string&)>& onChanged);

// Nearby-airport selector: a combo box listing every candidate as
// "ICAO (X.X nm)", nearest-first (candidates already arrive sorted that
// way). Returns true if the selection changed this frame.
bool RenderNearbyAirportSelector(const std::vector<core::NearbyCandidate>& candidates,
                                  std::optional<std::string>& selectedIcao);

// Generic labeled preset combo box (Options/Settings tab). Returns true
// and sets *outNewValue if the user
// picked a different preset than currentValue this frame.
template <typename T, std::size_t N>
bool RenderPresetSelector(const char* label, const std::array<T, N>& presets, T currentValue, const char* format,
                           T* outNewValue)
{
    std::size_t currentIndex = 0;
    for (std::size_t i = 0; i < presets.size(); ++i) {
        if (presets[i] == currentValue) {
            currentIndex = i;
            break;
        }
    }

    char preview[32];
    std::snprintf(preview, sizeof(preview), format, presets[currentIndex]);

    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (std::size_t i = 0; i < presets.size(); ++i) {
            char itemLabel[32];
            std::snprintf(itemLabel, sizeof(itemLabel), format, presets[i]);
            if (ImGui::Selectable(itemLabel, i == currentIndex)) {
                *outNewValue = presets[i];
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace trm::ui
