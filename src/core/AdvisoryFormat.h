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
// airport's shared wind_estimate, else kNone -- then collapses the two
// into one AdvisoryCategory::kBoth clause when they land on the *exact*
// same tier and the *exact* same runway set (order-independent). Any
// partial overlap or tier mismatch stays as two separate clauses, arrival
// before departure, so a confidence-tier difference is never silently
// lost. Returns one clause (collapsed) or two (separate).
std::vector<AdvisoryClause> BuildAdvisoryClauses(const AirportEntry& entry);

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

} // namespace trm::core
