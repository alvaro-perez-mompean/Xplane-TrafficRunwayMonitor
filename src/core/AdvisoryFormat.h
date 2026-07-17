#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/Aggregator.h"
#include "core/Format.h"
#include "core/WindEstimate.h"

// Natural-language traffic advisory sentence: a mini-ATIS-style summary of
// an AirportEntry's already-resolved arrivals/departures/wind data, built
// as structured clause data first (BuildAdvisoryClauses) so a plain-text
// renderer (FormatAdvisoryPlainText, wired into ui::Widgets) and a spoken-
// form renderer (FormatAdvisorySpoken, no real consumer yet) can each
// render the same underlying facts differently -- see
// notes/features/natural-language-traffic-advisory.md for the resolved
// design decisions this implements.

namespace trm::core {

// User-selectable airport-card display mode (Settings tab): the existing
// colored per-runway bullet lines, the new sentence, or both together.
enum class AdvisoryDisplayMode { kList, kNaturalLanguage, kBoth };

enum class AdvisoryCategory { kArrival, kDeparture, kBoth };

enum class AdvisoryTier { kActive, kHistory, kWindEstimate, kNone };

// One clause's worth of resolved facts -- data, not text. `runway_ids` is
// empty only when tier == kNone. `elapsed_sec` is only meaningful for
// kHistory (the single most-recent pick's age); `wind_source` only for
// kWindEstimate.
struct AdvisoryClause {
    AdvisoryCategory category = AdvisoryCategory::kArrival;
    AdvisoryTier tier = AdvisoryTier::kNone;
    std::vector<std::string> runway_ids;
    std::optional<double> elapsed_sec;
    std::optional<WindEstimateSource> wind_source;
};

// Resolves arrivals and departures independently -- active (ALL runways in
// CategoryResult::active, joined -- it's a vector, more than one can be
// active at once) if non-empty, else the single history pick, else the
// airport's shared wind_estimate, else kNone -- then applies one cross-
// category correction (see below) before collapsing the two into one
// AdvisoryCategory::kBoth clause when they land on the *exact* same tier
// and the *exact* same runway set (order-independent). Any partial overlap
// or tier mismatch stays as two separate clauses, arrival before departure,
// so a confidence-tier difference is never silently lost. Returns one
// clause (collapsed) or two (separate).
//
// Cross-category wind-estimate bias: the wind estimate is computed once
// per airport purely from wind-vs-heading, with no awareness of which
// runway the *other* category already has confirmed traffic on -- left
// alone, this can independently land on the physical opposite end of a
// runway the other category is actively using (e.g. arrivals landing 13,
// wind estimate favoring 31 for departures: same strip, opposite
// threshold, reads as a contradiction). When exactly that happens --
// this category resolves to kWindEstimate, the other category resolves to
// a single confirmed (active or history) runway, and `airport` is
// non-null -- this category's runway is overridden to match the other
// category's, via an exact core::FindOtherRunwayEndId lookup rather than
// any heading/number-family heuristic. That exactness matters at a
// parallel-runway airport (13L/31L and 13R/31R as two distinct physical
// strips): arrivals on 13L must NOT bias a departure wind estimate that
// independently favors 31R, since that's 13R's partner, a genuinely
// different runway, not a contradiction. The bias is skipped (left as the
// raw wind estimate) whenever the other category has more than one
// simultaneous active runway -- too ambiguous which one to compare
// against. `airport` is nullable; nullptr disables the bias entirely (no
// apt.dat data to look up the physical pairing from).
std::vector<AdvisoryClause> BuildAdvisoryClauses(const AirportEntry& entry, const Airport* airport = nullptr);

// Digit-form runway IDs ("landing runway 24L"). This is what
// ui::Widgets.cpp wires up. `currentWind`/`altimeterPa` are
// AirportEntry::current_wind/altimeter_pa -- passed separately since
// they aren't part of the clause list. Altimeter phraseology follows
// `pressureUnit` per real-world convention: "QNH nnnn" for kHpa,
// "altimeter nn.nn" for kInHg -- deliberately not core::FormatAltimeter's
// output, which appends a unit-name suffix ("hPa"/"inHg") that's never
// spoken aloud in either phraseology. Wind clause omitted if currentWind
// is nullopt; altimeter clause omitted if altimeterPa is nullopt.
std::string FormatAdvisoryPlainText(const std::vector<AdvisoryClause>& clauses,
                                     const std::optional<WindInfo>& currentWind, std::optional<double> altimeterPa,
                                     PressureUnit pressureUnit);

// Same sentence, phonetic runway IDs ("landing runway two four left") via
// SpokenRunwayId. No real consumer yet (groundwork for a possible future
// TTS/audio-callout feature) -- exists and is unit-tested regardless, per
// the note's "high effort" consumer-shape decision.
std::string FormatAdvisorySpoken(const std::vector<AdvisoryClause>& clauses,
                                  const std::optional<WindInfo>& currentWind, std::optional<double> altimeterPa,
                                  PressureUnit pressureUnit);

// "24L" -> "two four left", "06" -> "zero six", "31" -> "three one". Digits
// spelled out individually (not "twenty-four"), letter suffix (L/R/C)
// spoken as its own trailing word, omitted if the runway ID has none.
std::string SpokenRunwayId(const std::string& runwayId);

// Both plain-text variants RenderAirportCard's Both/Natural-language modes
// need, precomputed together -- see ResolveAdvisoryText. `with_wind_and_altimeter`
// is what Natural language mode shows (it has no separate header lines);
// `without_wind_and_altimeter` is what Both mode shows (its own header
// Wind:/Altimeter: lines already cover that, so the sentence stays
// runway-status-only there). Caching both means ui:: can switch which one
// it renders the instant the display-mode setting changes, without waiting
// on the next orchestration cycle.
struct ResolvedAdvisoryText {
    std::string with_wind_and_altimeter;
    std::string without_wind_and_altimeter;
};

// Runs BuildAdvisoryClauses once (passing `airport` through for its
// cross-category wind-estimate bias) and FormatAdvisoryPlainText twice
// (with and without the wind/altimeter tail) for `entry`. This is the
// entry point the ~1Hz orchestration cycle (Plugin.cpp) calls to populate
// ui::DisplayState -- per CLAUDE.md's ui/ layering rule, no core::
// computation (this included) may run from inside ui:: itself, so the
// result is cached and ui::Widgets only ever picks between the two
// already-formatted strings.
ResolvedAdvisoryText ResolveAdvisoryText(const AirportEntry& entry, PressureUnit pressureUnit,
                                          const Airport* airport = nullptr);

} // namespace trm::core
