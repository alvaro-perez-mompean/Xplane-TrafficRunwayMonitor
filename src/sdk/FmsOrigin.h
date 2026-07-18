#pragma once

#include <optional>
#include <string>

#include "XPLMDataAccess.h"

#include "core/FmsOrigin.h"

// Real XPLM SDK glue for FMS origin/destination lookup: native
// XPLMCountFMSEntries/XPLMGetFMSEntryInfo, with a ToLiss-specific override
// reading its own MCDU1 screen-mirror datarefs when available.

namespace trm::sdk {

struct FmsOriginDestination {
    std::optional<std::string> origin_icao;
    std::optional<std::string> destination_icao;
    // True whenever origin_icao/destination_icao was just (re)confirmed by
    // its source this cycle -- native FMS entries are read live every
    // call, so presence alone means fresh; the ToLiss MCDU path latches a
    // value across calls (see core::ToLissFmsState), so freshness there is
    // core::IsFresh() against core::ToLissFmsState::last_confirmed_at_sec
    // instead. Drives ui::DisplayState::origin_editable/destination_editable
    // in Plugin.cpp -- a field is only ever user-editable while its
    // source's value is stale.
    bool origin_fresh = false;
    bool destination_fresh = false;
};

class FmsOrigin {
public:
    // Prefers ToLiss's own MCDU1-screen-derived values when available
    // (resolved lazily, retried every call until found -- a one-time
    // resolution attempt can lose a load-order race against ToLiss's own
    // plugin, since plugin load order between separate X-Plane plugins
    // isn't guaranteed). Falls back to the native FMS entries for every
    // other aircraft, or before ToLiss's MCDU datarefs have ever resolved.
    //
    // Native path deliberately uses XPLMCountFMSEntries() - 1 for the
    // destination, NOT XPLMGetDestinationFMSEntry() -- that function
    // returns the FMS's current active-leg entry, not the destination (a
    // real, documented X-Plane SDK quirk confirmed against XPLMNavigation.h's
    // own description, and observed in-sim on a ToLiss Airbus returning the
    // origin instead).
    FmsOriginDestination Resolve(double nowSec);

private:
    void ResolveToLissMcduRefs();

    XPLMDataRef mcdu1_title_ref_ = nullptr;
    XPLMDataRef mcdu1_cont1b_ref_ = nullptr;
    XPLMDataRef mcdu1_label1_ref_ = nullptr;

    core::ToLissFmsState toliss_state_;
};

} // namespace trm::sdk
