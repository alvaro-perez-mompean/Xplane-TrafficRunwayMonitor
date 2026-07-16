#include "ui/Widgets.h"

#include <algorithm>
#include <cmath>

#include "core/GeoMath.h"

namespace trm::ui {

namespace {

bool RunwayIsActive(const core::CategoryResult& category, const std::string& runwayId)
{
    for (const auto& runway : category.active) {
        if (runway.runway_id == runwayId) {
            return true;
        }
    }
    return false;
}

} // namespace

ImU32 RunwayStatusColor(const core::AirportEntry& entry, const std::string& runwayId)
{
    if (RunwayIsActive(entry.departures, runwayId) || RunwayIsActive(entry.arrivals, runwayId)) {
        return kColorConfirmed;
    }
    if (entry.wind_estimate.has_value() && entry.wind_estimate->runway_id == runwayId &&
        (entry.departures.NeedsWindEstimate() || entry.arrivals.NeedsWindEstimate())) {
        return kColorWindEstimate;
    }
    return kColorWaiting;
}

void RenderLengthSuffix(const std::optional<double>& lengthFt)
{
    if (!lengthFt.has_value()) {
        return;
    }
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
    ImGui::Text("(%d ft)", static_cast<int>(*lengthFt + 0.5));
    ImGui::PopStyleColor();
}

void RenderCategorySection(const char* title, const core::CategoryResult& category,
                            const std::optional<core::WindEstimateResult>& windEstimate)
{
    ImGui::Text("%s:", title);

    if (!category.active.empty()) {
        for (const auto& runway : category.active) {
            ImGui::PushStyleColor(ImGuiCol_Text, kColorConfirmed);
            ImGui::Text("    * %s (%d, last %s)", runway.runway_id.c_str(), runway.count,
                        core::FormatAgo(runway.elapsed_sec).c_str());
            ImGui::PopStyleColor();
            RenderLengthSuffix(runway.length_ft);
        }
    } else if (category.history.has_value()) {
        const auto& history = *category.history;
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
        ImGui::Text("    %s (%d, last %s)", history.runway_id.c_str(), history.count,
                    core::FormatAgo(history.elapsed_sec).c_str());
        ImGui::PopStyleColor();
        RenderLengthSuffix(history.length_ft);
    } else if (category.NeedsWindEstimate() && windEstimate.has_value()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWindEstimate);
        ImGui::Text("    ~ %s (wind)", windEstimate->runway_id.c_str());
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
        ImGui::TextUnformatted("(?)");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
            ImGui::Text("Wind-based guess only, not confirmed by traffic yet. Source: %s.",
                        core::WindEstimateSourceLabel(windEstimate->source).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    } else {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
        ImGui::TextUnformatted("    -- waiting for traffic");
        ImGui::PopStyleColor();
    }
}

void RenderAirportCard(const core::AirportEntry& entry, bool showRawMetar, core::PressureUnit pressureUnit,
                        bool showHeader)
{
    if (showHeader) {
        if (entry.name.has_value() && entry.distance_nm.has_value()) {
            ImGui::Text("%s - %s (%.1f nm)", entry.icao.c_str(), entry.name->c_str(), *entry.distance_nm);
        } else if (entry.name.has_value()) {
            ImGui::Text("%s - %s", entry.icao.c_str(), entry.name->c_str());
        } else if (entry.distance_nm.has_value()) {
            ImGui::Text("%s (%.1f nm)", entry.icao.c_str(), *entry.distance_nm);
        } else {
            ImGui::TextUnformatted(entry.icao.c_str());
        }
    }

    if (entry.current_wind.has_value()) {
        const core::WindInfo& wind = *entry.current_wind;
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWind);
        if (wind.is_calm) {
            ImGui::TextUnformatted("  Wind: Calm");
        } else {
            ImGui::Text("  Wind: %.0f\xC2\xB0T @ %.0fkt", wind.direction_true_deg, wind.speed_kt);
        }
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
        ImGui::TextUnformatted("(?)");
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
            ImGui::Text("Source: %s.", core::WindEstimateSourceLabel(wind.source).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }

    if (entry.altimeter_pa.has_value()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWind);
        ImGui::Text("  Altimeter: %s", core::FormatAltimeter(*entry.altimeter_pa, pressureUnit).c_str());
        ImGui::PopStyleColor();
    }

    RenderCategorySection("  Departures", entry.departures, entry.wind_estimate);
    RenderCategorySection("  Arrivals", entry.arrivals, entry.wind_estimate);

    if (showRawMetar && entry.metar.has_value()) {
        ImGui::Text("  METAR: %s", entry.metar->c_str());
    }
}

void RenderRunwayDiagram(const core::Airport* airport, const core::AirportEntry* entry, float diameter)
{
    if (airport == nullptr || airport->runways.empty()) {
        return;
    }

    ImVec2 topLeft = ImGui::GetCursorScreenPos();
    // Extra strip to the right of the compass circle reserved for the
    // windsock -- its mount point is fixed there regardless of wind
    // direction, unlike the compass circle geometry it sits beside.
    constexpr float kWindsockStripWidth = 50.0f;
    ImGui::Dummy(ImVec2(diameter + kWindsockStripWidth, diameter));
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    ImVec2 center(topLeft.x + diameter * 0.5f, topLeft.y + diameter * 0.5f);
    float radiusPx = diameter * 0.5f - 14.0f;

    // Project every threshold onto a local, north-up tangent plane centered
    // on the airport's reference point (apt.dat has no local coordinate
    // system of its own), then pick the one scale factor that fits the
    // furthest threshold inside radiusPx. That keeps every runway's real
    // relative position, spacing, and length intact -- parallel runways
    // land at their true lateral offset instead of stacking on top of each
    // other the way a fixed-radius, angle-only plot would.
    std::vector<core::LocalOffsetFt> offsets;
    offsets.reserve(airport->runways.size());
    double maxDistFt = 0.0;
    for (const auto& runwayEnd : airport->runways) {
        core::LocalOffsetFt offset = core::LocalOffsetFromReference(runwayEnd.lat_deg, runwayEnd.lon_deg,
                                                                     airport->ref_lat_deg, airport->ref_lon_deg);
        maxDistFt = std::max(maxDistFt, std::sqrt(offset.east_ft * offset.east_ft + offset.north_ft * offset.north_ft));
        offsets.push_back(offset);
    }
    const float ftToPx = (maxDistFt > 1e-6) ? static_cast<float>(radiusPx / maxDistFt) : 0.0f;

    auto ToPixel = [&](const core::LocalOffsetFt& offset) {
        return ImVec2(center.x + static_cast<float>(offset.east_ft) * ftToPx,
                      center.y - static_cast<float>(offset.north_ft) * ftToPx);
    };
    auto DrawLabel = [&](const ImVec2& point, ImU32 color, const std::string& id) {
        ImVec2 labelPos(point.x + ((point.x >= center.x) ? 2.0f : -18.0f), point.y - 6.0f);
        drawList->AddText(labelPos, color, id.c_str());
    };

    // AptDat.cpp always pushes a physical runway's two ends back-to-back
    // (see ParseAptDat's row-100 handling), so consecutive pairs here are
    // always the two thresholds of the same runway.
    for (std::size_t i = 0; i + 1 < airport->runways.size(); i += 2) {
        const auto& endA = airport->runways[i];
        const auto& endB = airport->runways[i + 1];

        ImU32 colorA = (entry != nullptr) ? RunwayStatusColor(*entry, endA.id) : kColorWaiting;
        ImU32 colorB = (entry != nullptr) ? RunwayStatusColor(*entry, endB.id) : kColorWaiting;
        ImVec2 pixelA = ToPixel(offsets[i]);
        ImVec2 pixelB = ToPixel(offsets[i + 1]);
        ImVec2 midpoint((pixelA.x + pixelB.x) * 0.5f, (pixelA.y + pixelB.y) * 0.5f);

        drawList->AddLine(midpoint, pixelA, colorA, 2.0f);
        drawList->AddLine(midpoint, pixelB, colorB, 2.0f);

        DrawLabel(pixelA, colorA, endA.id);
        DrawLabel(pixelB, colorB, endB.id);
    }

    // Windsock: mount point fixed in the reserved strip beside the circle,
    // regardless of wind direction (unlike the old arrow, which slid around
    // the compass rim). Only the cone rotates/extends with wind; it streams
    // downwind (mouth into the wind, tail pointing away from it) -- the
    // inverse of the old arrow's "points at the source" convention. Kept in
    // the same north-up frame as the compass circle so its angle still
    // visually correlates with which runway is into-wind. Rendered even when
    // calm (drooping limp), unlike the old arrow which was hidden entirely.
    if (entry != nullptr && entry->current_wind.has_value()) {
        const core::WindInfo& wind = *entry->current_wind;

        constexpr float kMountOffsetX = 25.0f; // gap from the circle's right edge
        constexpr float kShortLen = 9.0f;  // calm: short, near-vertical droop
        constexpr float kFullLen = 30.0f;  // >=15kt: fully extended
        constexpr float kMouthHalfWidth = 6.0f;
        constexpr float kTailHalfWidth = 1.5f;
        constexpr int kBands = 5;
        constexpr double kFullExtensionKt = 15.0; // FAA/ICAO reference speed

        const ImVec2 mount(topLeft.x + diameter + kMountOffsetX, topLeft.y + diameter * 0.5f);

        const double t = std::clamp(wind.speed_kt / kFullExtensionKt, 0.0, 1.0);

        // Limp state droops straight down on screen (gravity, not compass
        // direction); full extension streams along the downwind bearing.
        const double downwindRad = core::ToRadians(wind.direction_true_deg + 180.0);
        const float fullX = static_cast<float>(std::sin(downwindRad));
        const float fullY = static_cast<float>(-std::cos(downwindRad));

        float dirX = static_cast<float>(t) * fullX;
        float dirY = static_cast<float>(1.0 - t) * 1.0f + static_cast<float>(t) * fullY;
        const float dirLen = std::sqrt(dirX * dirX + dirY * dirY);
        if (dirLen > 1e-4f) {
            dirX /= dirLen;
            dirY /= dirLen;
        }
        const ImVec2 perp(-dirY, dirX);
        const float coneLen = static_cast<float>((1.0 - t) * kShortLen + t * kFullLen);

        for (int band = 0; band < kBands; ++band) {
            const float f0 = static_cast<float>(band) / kBands;
            const float f1 = static_cast<float>(band + 1) / kBands;
            const float halfW0 = kMouthHalfWidth + (kTailHalfWidth - kMouthHalfWidth) * f0;
            const float halfW1 = kMouthHalfWidth + (kTailHalfWidth - kMouthHalfWidth) * f1;
            const ImVec2 center0(mount.x + dirX * coneLen * f0, mount.y + dirY * coneLen * f0);
            const ImVec2 center1(mount.x + dirX * coneLen * f1, mount.y + dirY * coneLen * f1);
            const ImVec2 near0(center0.x + perp.x * halfW0, center0.y + perp.y * halfW0);
            const ImVec2 near1(center0.x - perp.x * halfW0, center0.y - perp.y * halfW0);
            const ImVec2 far0(center1.x + perp.x * halfW1, center1.y + perp.y * halfW1);
            const ImVec2 far1(center1.x - perp.x * halfW1, center1.y - perp.y * halfW1);
            const ImU32 bandColor = (band % 2 == 0) ? kColorWindsockOrange : kColorWindsockWhite;
            drawList->AddQuadFilled(near0, far0, far1, near1, bandColor);
        }

        // Dedicated hit box for the icon itself (it's ImDrawList-drawn, not a
        // real widget) so hovering it shows the same source tooltip as the
        // "Wind: ... (?)" text line above, without duplicating that text
        // line's own hover handling.
        const float reach = kFullLen + 10.0f;
        const ImVec2 hitTopLeft(mount.x - reach, mount.y - reach);
        ImGui::SetCursorScreenPos(hitTopLeft);
        ImGui::PushID(entry->icao.c_str());
        ImGui::InvisibleButton("##windsock_hover", ImVec2(reach * 2.0f, reach * 2.0f));
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 20.0f);
            ImGui::Text("Source: %s.", core::WindEstimateSourceLabel(wind.source).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos(ImVec2(topLeft.x, topLeft.y + diameter));
    }
}

void RenderEventHistory(const std::vector<core::RunwayEventSummary>& events)
{
    if (events.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
        ImGui::TextUnformatted("-- no confirmed departures/arrivals in this window --");
        ImGui::PopStyleColor();
        return;
    }

    if (ImGui::BeginTable("##event_history", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Time");
        ImGui::TableSetupColumn("Airport");
        ImGui::TableSetupColumn("Runway");
        ImGui::TableSetupColumn("Aircraft");
        ImGui::TableSetupColumn("Type");
        ImGui::TableAutoHeaders();

        for (const auto& event : events) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(core::FormatAgo(event.elapsed_sec).c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(event.icao.c_str());

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(event.runway_id.c_str());

            ImGui::TableSetColumnIndex(3);
            if (event.callsign.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, kColorWaiting);
                ImGui::TextUnformatted("--");
                ImGui::PopStyleColor();
            } else {
                ImGui::TextUnformatted(event.callsign.c_str());
            }

            ImGui::TableSetColumnIndex(4);
            const bool isDeparture = (event.category == core::SightingCategory::kDeparture);
            ImGui::PushStyleColor(ImGuiCol_Text, kColorConfirmed);
            ImGui::TextUnformatted(isDeparture ? "Departure" : "Arrival");
            ImGui::PopStyleColor();
        }
        ImGui::EndTable();
    }
}

namespace {

// Builds unbounded (no truncation) since airport names have no fixed max
// length worth guessing at -- unlike the fixed-size snprintf buffers used
// elsewhere in this file for short numeric-only labels.
std::string FormatNearbyLabel(const core::NearbyCandidate& candidate)
{
    char distanceSuffix[32];
    std::snprintf(distanceSuffix, sizeof(distanceSuffix), " (%.1f nm)", candidate.distance_nm);

    std::string label = candidate.icao;
    if (!candidate.name.empty()) {
        label += " - ";
        label += candidate.name;
    }
    label += distanceSuffix;
    return label;
}

} // namespace

bool RenderNearbyAirportSelector(const std::vector<core::NearbyCandidate>& candidates,
                                  std::optional<std::string>& selectedIcao)
{
    const core::NearbyCandidate* selectedCandidate = nullptr;
    for (const auto& candidate : candidates) {
        if (selectedIcao.has_value() && *selectedIcao == candidate.icao) {
            selectedCandidate = &candidate;
            break;
        }
    }

    const std::string preview = (selectedCandidate != nullptr) ? FormatNearbyLabel(*selectedCandidate) : "Select airport...";

    bool changed = false;
    if (ImGui::BeginCombo("##nearby_airport_selector", preview.c_str())) {
        for (const auto& candidate : candidates) {
            bool selected = selectedIcao.has_value() && *selectedIcao == candidate.icao;
            const std::string label = FormatNearbyLabel(candidate);

            if (ImGui::Selectable(label.c_str(), selected)) {
                selectedIcao = candidate.icao;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace trm::ui
