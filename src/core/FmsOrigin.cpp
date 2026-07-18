#include "core/FmsOrigin.h"

#include <regex>

namespace trm::core {

std::optional<std::pair<std::string, std::string>> ParseToLissInitPageFromTo(const ToLissMcduSnapshot& snapshot)
{
    if (snapshot.title.find("INIT") == std::string::npos) {
        return std::nullopt;
    }
    if (snapshot.label1.find("FROM/TO") == std::string::npos) {
        return std::nullopt;
    }

    static const std::regex kFromToPattern(R"(([A-Z]{4})/([A-Z]{4}))");
    std::smatch match;
    if (!std::regex_search(snapshot.cont1b, match, kFromToPattern)) {
        return std::nullopt;
    }

    return std::make_pair(match[1].str(), match[2].str());
}

void UpdateToLissFmsState(ToLissFmsState& state, const ToLissMcduSnapshot& snapshot, double nowSec)
{
    const auto parsed = ParseToLissInitPageFromTo(snapshot);
    if (parsed) {
        state.last_confirmed_origin = parsed->first;
        state.last_confirmed_destination = parsed->second;
        state.last_confirmed_at_sec = nowSec;
    }
    // else: leave state untouched -- hold the last confirmed value.
}

bool IsFresh(std::optional<double> lastConfirmedAtSec, double nowSec, double thresholdSec)
{
    if (!lastConfirmedAtSec.has_value()) {
        return false;
    }
    return (nowSec - *lastConfirmedAtSec) <= thresholdSec;
}

NativeFmsOriginDestination ResolveNativeFmsOriginDestination(int entryCount, const FmsEntryInfo& originEntry,
                                                               const FmsEntryInfo& destinationEntry)
{
    NativeFmsOriginDestination result;
    if (entryCount <= 0) {
        return result;
    }
    if (originEntry.is_airport && !originEntry.id.empty()) {
        result.origin_icao = originEntry.id;
    }
    if (destinationEntry.is_airport && !destinationEntry.id.empty()) {
        result.destination_icao = destinationEntry.id;
    }
    return result;
}

std::optional<std::string> ResolveEffectiveIcao(bool fresh, const std::optional<std::string>& sourceIcao,
                                                 const std::optional<std::string>& overrideIcao)
{
    return fresh ? sourceIcao : overrideIcao;
}

} // namespace trm::core
