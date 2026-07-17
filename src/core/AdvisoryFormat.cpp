#include "core/AdvisoryFormat.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <functional>

namespace trm::core {

namespace {

std::vector<std::string> ActiveRunwayIds(const std::vector<RunwaySightingSummary>& active)
{
    std::vector<std::string> ids;
    ids.reserve(active.size());
    for (const auto& runway : active) {
        ids.push_back(runway.runway_id);
    }
    return ids;
}

AdvisoryClause ResolveCategoryClause(AdvisoryCategory category, const CategoryResult& result,
                                      const std::optional<WindEstimateResult>& windEstimate)
{
    AdvisoryClause clause;
    clause.category = category;

    if (!result.active.empty()) {
        clause.tier = AdvisoryTier::kActive;
        clause.runway_ids = ActiveRunwayIds(result.active);
        return clause;
    }
    if (result.history.has_value()) {
        clause.tier = AdvisoryTier::kHistory;
        clause.runway_ids = {result.history->runway_id};
        clause.elapsed_sec = result.history->elapsed_sec;
        return clause;
    }
    if (result.NeedsWindEstimate() && windEstimate.has_value()) {
        clause.tier = AdvisoryTier::kWindEstimate;
        clause.runway_ids = {windEstimate->runway_id};
        clause.wind_source = windEstimate->source;
        return clause;
    }
    clause.tier = AdvisoryTier::kNone;
    return clause;
}

bool SameRunwaySet(const std::vector<std::string>& a, const std::vector<std::string>& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    std::vector<std::string> sortedA = a;
    std::vector<std::string> sortedB = b;
    std::sort(sortedA.begin(), sortedA.end());
    std::sort(sortedB.begin(), sortedB.end());
    return sortedA == sortedB;
}

std::string JoinRunwayIds(const std::vector<std::string>& ids)
{
    if (ids.empty()) {
        return {};
    }
    if (ids.size() == 1) {
        return ids[0];
    }
    std::string joined;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i > 0) {
            joined += (i + 1 == ids.size()) ? " and " : ", ";
        }
        joined += ids[i];
    }
    return joined;
}

std::string RunwayNoun(const std::vector<std::string>& ids)
{
    return ids.size() > 1 ? "runways" : "runway";
}

std::string CategoryVerbActive(AdvisoryCategory category)
{
    switch (category) {
        case AdvisoryCategory::kArrival:
            return "currently landing";
        case AdvisoryCategory::kDeparture:
            return "currently departing";
        case AdvisoryCategory::kBoth:
            return "currently landing and departing";
    }
    return "currently active on";
}

std::string CategoryVerbHistory(AdvisoryCategory category)
{
    switch (category) {
        case AdvisoryCategory::kArrival:
            return "recently landed";
        case AdvisoryCategory::kDeparture:
            return "recently departed";
        case AdvisoryCategory::kBoth:
            return "recently landed and departed";
    }
    return "recently used";
}

std::string CategoryNoun(AdvisoryCategory category)
{
    switch (category) {
        case AdvisoryCategory::kArrival:
            return "arrivals";
        case AdvisoryCategory::kDeparture:
            return "departures";
        case AdvisoryCategory::kBoth:
            return "arrivals and departures";
    }
    return "traffic";
}

// Sentence-specific caveat, distinct from WindEstimateSourceLabel (which is
// written for the tooltip's full sentence-fragment style). Own-station/
// station readings are real, specific weather data -- trustworthy enough
// that flagging them in the sentence reads as unnecessary hedging.
// Regional/aircraft-position readings are rougher approximations, worth a
// short caveat. nullopt means "say nothing" (the trustworthy tiers).
std::optional<std::string> AdvisoryWindSourceCaveat(WindEstimateSource source)
{
    switch (source) {
        case WindEstimateSource::kOwnStation:
        case WindEstimateSource::kStation:
            return std::nullopt;
        case WindEstimateSource::kRegional:
            return "regional estimate";
        case WindEstimateSource::kAircraftPosition:
            return "aircraft-based estimate";
    }
    return std::nullopt;
}

using RunwayIdFormatter = std::function<std::string(const std::string&)>;

std::string FormatClause(const AdvisoryClause& clause, const RunwayIdFormatter& formatRunwayId)
{
    std::vector<std::string> ids;
    ids.reserve(clause.runway_ids.size());
    for (const auto& id : clause.runway_ids) {
        ids.push_back(formatRunwayId(id));
    }

    switch (clause.tier) {
        case AdvisoryTier::kActive:
            return CategoryVerbActive(clause.category) + " " + RunwayNoun(ids) + " " + JoinRunwayIds(ids);
        case AdvisoryTier::kHistory: {
            std::string text = CategoryVerbHistory(clause.category) + " " + RunwayNoun(ids) + " " + JoinRunwayIds(ids);
            if (clause.elapsed_sec.has_value()) {
                text += " (" + FormatAgo(*clause.elapsed_sec) + ")";
            }
            return text;
        }
        case AdvisoryTier::kWindEstimate: {
            std::string text = "wind favors " + RunwayNoun(ids) + " " + JoinRunwayIds(ids) + " for " +
                                CategoryNoun(clause.category);
            if (clause.wind_source.has_value()) {
                if (const auto caveat = AdvisoryWindSourceCaveat(*clause.wind_source)) {
                    text += " (" + *caveat + ")";
                }
            }
            return text;
        }
        case AdvisoryTier::kNone:
            if (clause.category == AdvisoryCategory::kBoth) {
                return "no traffic information";
            }
            return "no traffic information for " + CategoryNoun(clause.category);
    }
    return {};
}

constexpr double kPaToInHg = 0.0002953;
constexpr double kPaToHpa = 0.01;

// Deliberately separate from core::FormatAltimeter: that function's output
// carries a unit-name suffix ("29.92 inHg", "1013 hPa") meant for a static
// UI readout, whereas real ATC phraseology never speaks the unit -- it
// says "altimeter two niner niner two" or "QNH one zero one three", the
// leading word alone disambiguates the unit.
std::string FormatPressurePhraseology(double pressurePa, PressureUnit unit)
{
    char buf[32];
    if (unit == PressureUnit::kInHg) {
        std::snprintf(buf, sizeof(buf), "altimeter %.2f", pressurePa * kPaToInHg);
    } else {
        std::snprintf(buf, sizeof(buf), "QNH %.0f", pressurePa * kPaToHpa);
    }
    return buf;
}

std::string JoinParts(const std::vector<std::string>& parts)
{
    std::string joined;
    for (const auto& part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += part;
    }
    if (joined.empty()) {
        return joined;
    }
    joined[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(joined[0])));
    return joined + ".";
}

std::string FormatAdvisory(const std::vector<AdvisoryClause>& clauses, const std::optional<WindInfo>& currentWind,
                            std::optional<double> altimeterPa, PressureUnit pressureUnit,
                            const RunwayIdFormatter& formatRunwayId)
{
    std::vector<std::string> parts;
    for (const auto& clause : clauses) {
        parts.push_back(FormatClause(clause, formatRunwayId));
    }

    if (currentWind.has_value()) {
        if (currentWind->is_calm) {
            parts.push_back("wind calm");
        } else {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "wind %.0f at %.0f", currentWind->direction_true_deg,
                          currentWind->speed_kt);
            parts.push_back(buf);
        }
    }

    if (altimeterPa.has_value()) {
        parts.push_back(FormatPressurePhraseology(*altimeterPa, pressureUnit));
    }

    return JoinParts(parts);
}

} // namespace

std::vector<AdvisoryClause> BuildAdvisoryClauses(const AirportEntry& entry)
{
    AdvisoryClause arrival = ResolveCategoryClause(AdvisoryCategory::kArrival, entry.arrivals, entry.wind_estimate);
    AdvisoryClause departure =
        ResolveCategoryClause(AdvisoryCategory::kDeparture, entry.departures, entry.wind_estimate);

    const bool sameTier = arrival.tier == departure.tier;
    const bool sameWindSource = arrival.wind_source == departure.wind_source;
    if (sameTier && sameWindSource && SameRunwaySet(arrival.runway_ids, departure.runway_ids)) {
        AdvisoryClause combined = arrival;
        combined.category = AdvisoryCategory::kBoth;
        return {combined};
    }
    return {arrival, departure};
}

std::string FormatAdvisoryPlainText(const std::vector<AdvisoryClause>& clauses,
                                     const std::optional<WindInfo>& currentWind, std::optional<double> altimeterPa,
                                     PressureUnit pressureUnit)
{
    return FormatAdvisory(clauses, currentWind, altimeterPa, pressureUnit,
                           [](const std::string& id) { return id; });
}

std::string FormatAdvisorySpoken(const std::vector<AdvisoryClause>& clauses,
                                  const std::optional<WindInfo>& currentWind, std::optional<double> altimeterPa,
                                  PressureUnit pressureUnit)
{
    return FormatAdvisory(clauses, currentWind, altimeterPa, pressureUnit,
                           [](const std::string& id) { return SpokenRunwayId(id); });
}

std::string SpokenRunwayId(const std::string& runwayId)
{
    static const char* const kDigitWords[10] = {"zero", "one", "two",   "three", "four",
                                                 "five", "six", "seven", "eight", "nine"};

    std::string digits;
    char suffix = '\0';
    for (char c : runwayId) {
        if (std::isdigit(static_cast<unsigned char>(c))) {
            digits += c;
        } else {
            suffix = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }

    std::string spoken;
    for (char c : digits) {
        if (!spoken.empty()) {
            spoken += " ";
        }
        spoken += kDigitWords[c - '0'];
    }

    switch (suffix) {
        case 'L':
            spoken += " left";
            break;
        case 'R':
            spoken += " right";
            break;
        case 'C':
            spoken += " center";
            break;
        default:
            break;
    }
    return spoken;
}

} // namespace trm::core
