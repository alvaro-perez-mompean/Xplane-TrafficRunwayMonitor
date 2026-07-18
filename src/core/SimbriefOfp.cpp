#include "core/SimbriefOfp.h"

#include <regex>

namespace trm::core {

namespace {

std::optional<std::string> ExtractIcaoCode(const std::string& json, const std::string& objectKey)
{
    const std::regex objectPattern("\"" + objectKey + "\"\\s*:\\s*\\{([^}]*)\\}");
    std::smatch objectMatch;
    if (!std::regex_search(json, objectMatch, objectPattern)) {
        return std::nullopt;
    }

    static const std::regex kIcaoCodePattern("\"icao_code\"\\s*:\\s*\"([A-Z0-9]{4})\"");
    std::smatch icaoMatch;
    const std::string objectBody = objectMatch[1].str();
    if (!std::regex_search(objectBody, icaoMatch, kIcaoCodePattern)) {
        return std::nullopt;
    }
    return icaoMatch[1].str();
}

} // namespace

SimbriefOriginDestination ParseSimbriefOfp(const std::string& json)
{
    SimbriefOriginDestination result;
    result.origin_icao = ExtractIcaoCode(json, "origin");
    result.destination_icao = ExtractIcaoCode(json, "destination");
    return result;
}

} // namespace trm::core
