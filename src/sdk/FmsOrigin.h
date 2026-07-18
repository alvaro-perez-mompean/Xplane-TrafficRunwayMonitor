#pragma once

#include <optional>
#include <string>

#include "XPLMDataAccess.h"

// Real XPLM SDK glue for FMS origin/destination lookup: native
// XPLMCountFMSEntries/XPLMGetFMSEntryInfo.

namespace trm::sdk {

struct FmsOriginDestination {
    std::optional<std::string> origin_icao;
    std::optional<std::string> destination_icao;
    // True whenever origin_icao/destination_icao resolved a matching entry
    // this cycle -- native FMS entries are read live every call, so
    // presence alone means fresh, no grace period. Drives
    // ui::DisplayState::origin_editable/destination_editable in Plugin.cpp
    // -- a field is only ever user-editable while its source has no
    // matching entry.
    bool origin_fresh = false;
    bool destination_fresh = false;
};

class FmsOrigin {
public:
    // Reads X-Plane's native FMS flight plan entries every call.
    //
    // Deliberately uses XPLMCountFMSEntries() - 1 for the destination, NOT
    // XPLMGetDestinationFMSEntry() -- that function returns the FMS's
    // current active-leg entry, not the destination (a real, documented
    // X-Plane SDK quirk confirmed against XPLMNavigation.h's own
    // description, and observed in-sim on a ToLiss Airbus returning the
    // origin instead).
    FmsOriginDestination Resolve();
};

} // namespace trm::sdk
