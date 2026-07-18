#include "sdk/FmsOrigin.h"

#include "XPLMNavigation.h"

#include "core/FmsOrigin.h"

namespace trm::sdk {

FmsOriginDestination FmsOrigin::Resolve()
{
    const int entryCount = XPLMCountFMSEntries();

    core::FmsEntryInfo originEntry;
    core::FmsEntryInfo destinationEntry;
    if (entryCount > 0) {
        XPLMNavType origType;
        char origId[256] = {0};
        XPLMGetFMSEntryInfo(0, &origType, origId, nullptr, nullptr, nullptr, nullptr);
        originEntry.is_airport = (origType == xplm_Nav_Airport);
        originEntry.id = origId;

        XPLMNavType destType;
        char destId[256] = {0};
        XPLMGetFMSEntryInfo(entryCount - 1, &destType, destId, nullptr, nullptr, nullptr, nullptr);
        destinationEntry.is_airport = (destType == xplm_Nav_Airport);
        destinationEntry.id = destId;
    }

    const core::NativeFmsOriginDestination result =
        core::ResolveNativeFmsOriginDestination(entryCount, originEntry, destinationEntry);
    return FmsOriginDestination{result.origin_icao, result.destination_icao, result.origin_icao.has_value(),
                                 result.destination_icao.has_value()};
}

} // namespace trm::sdk
