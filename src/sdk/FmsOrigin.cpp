#include "sdk/FmsOrigin.h"

#include "XPLMNavigation.h"

#include <vector>

namespace trm::sdk {

namespace {

// MCDU1 screen-mirror datarefs are "Data" (byte-array/string) type,
// read via XPLMGetDatab. 256 bytes is comfortably more than one MCDU line
// needs; XPLMGetDatab clips to whatever's actually copied, so an
// oversized buffer here is harmless.
std::string ReadStringDataref(XPLMDataRef dr)
{
    if (!dr) {
        return {};
    }
    constexpr int kMaxBytes = 256;
    std::vector<char> buf(kMaxBytes, 0);
    const int copied = XPLMGetDatab(dr, buf.data(), 0, kMaxBytes);
    if (copied <= 0) {
        return {};
    }
    return std::string(buf.data(), static_cast<size_t>(copied));
}

} // namespace

void FmsOrigin::ResolveToLissMcduRefs()
{
    if (mcdu1_title_ref_ && mcdu1_cont1b_ref_ && mcdu1_label1_ref_) {
        return;
    }
    mcdu1_title_ref_ = XPLMFindDataRef("AirbusFBW/MCDU1titlew");
    mcdu1_cont1b_ref_ = XPLMFindDataRef("AirbusFBW/MCDU1cont1b");
    // Confirmed live (label1w = "CO RTE    FROM/TO" on the INIT page) --
    // required alongside titlew as a second, independent confirmation that
    // line 1 is actually the FROM/TO row before cont1b's content is
    // trusted, rather than relying on the page title alone.
    mcdu1_label1_ref_ = XPLMFindDataRef("AirbusFBW/MCDU1label1w");
}

FmsOriginDestination FmsOrigin::Resolve()
{
    ResolveToLissMcduRefs();

    if (mcdu1_title_ref_ && mcdu1_cont1b_ref_ && mcdu1_label1_ref_) {
        core::ToLissMcduSnapshot snapshot;
        snapshot.title = ReadStringDataref(mcdu1_title_ref_);
        snapshot.label1 = ReadStringDataref(mcdu1_label1_ref_);
        snapshot.cont1b = ReadStringDataref(mcdu1_cont1b_ref_);

        core::UpdateToLissFmsState(toliss_state_, snapshot);

        return FmsOriginDestination{toliss_state_.last_confirmed_origin, toliss_state_.last_confirmed_destination};
    }

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
    return FmsOriginDestination{result.origin_icao, result.destination_icao};
}

} // namespace trm::sdk
