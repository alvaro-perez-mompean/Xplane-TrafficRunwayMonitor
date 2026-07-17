#pragma once

#include <optional>

#include "core/AptDat.h"

// Locates and parses X-Plane's own default global-airport apt.dat once at
// startup.
//
// Real XPLMGetSystemPath call + filesystem access -- thin glue, not
// unit-tested; the parsing itself (core::ParseAptDat) already is.

namespace trm::sdk {

std::optional<core::AirportDatabase> LoadDefaultAptDat();

// LoadDefaultAptDat() plus any custom-scenery apt.dat overrides, merged by
// scenery_packs.ini priority order (see core::ParseScenerypacksIni /
// core::MergeAirportDatabases for the pure-logic half of this). Falls back
// to the default database alone if scenery_packs.ini can't be opened (e.g.
// a non-standard install layout) -- same graceful-degradation shape as
// LoadDefaultAptDat() itself returning nullopt on a missing file.
//
// This is the function XPluginEnable should call; LoadDefaultAptDat() is
// exposed separately because this function is built on top of it and
// because it's still useful on its own (e.g. future manual "reload"
// tooling that wants to re-resolve just the default database).
std::optional<core::AirportDatabase> LoadMergedAptDat();

} // namespace trm::sdk
