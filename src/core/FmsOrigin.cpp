#include "core/FmsOrigin.h"

namespace trm::core {

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
