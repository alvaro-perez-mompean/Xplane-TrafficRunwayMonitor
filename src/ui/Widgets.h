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

// Flight Plan tab: a 4-char, uppercase ICAO ImGui::InputText. While
// `editable` is false, mirrors `effectiveIcao` (the live source value) every
// frame and is read-only -- it isn't a manual entry in that state, so no
// callback fires. The instant `editable` flips true (detected via
// `wasEditable`, caller-owned so the transition survives across frames),
// the buffer is mirrored from `effectiveIcao` exactly once more -- normally
// blank at that point, since Plugin.cpp has nothing fresh and no override
// yet -- so the field doesn't keep showing the old locked ICAO as if it
// were still in effect. Every frame after that (while still editable and
// not the unlock frame) the buffer is left alone for the user to type in;
// each edit calls `onChanged` immediately with the buffer's current content
// (same synchronous-callback pattern as RenderNearbyAirportSelector's
// caller), no separate "confirm"/blur step. A one-line dim status
// explaining why the field is locked/editable renders underneath.
// `buf`/`bufSize` is the caller-owned InputText buffer -- ImGui itself is
// immediate-mode and doesn't own text-editing state across frames.
void RenderIcaoOverrideField(const char* label, char* buf, std::size_t bufSize, bool editable, bool& wasEditable,
                              const std::optional<std::string>& effectiveIcao,
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
