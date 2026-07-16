#pragma once

#include <optional>

#include "core/AptDat.h"

// Locates and parses X-Plane's own default global-airport apt.dat once at
// startup. Only the default scenery apt.dat is read; custom scenery
// overrides are not consulted (documented limitation, not an oversight).
//
// Real XPLMGetSystemPath call + filesystem access -- thin glue, not
// unit-tested; the parsing itself (core::ParseAptDat) already is.

namespace trm::sdk {

std::optional<core::AirportDatabase> LoadDefaultAptDat();

} // namespace trm::sdk
