#pragma once

#include <optional>
#include <string>

#include "core/Cifp.h"

// Locates and parses X-Plane's per-airport CIFP terminal-procedure data
// (SID/STAR/approach) for one ICAO.
//
// Real XPLMGetSystemPath call + filesystem access -- thin glue, not
// unit-tested; the parsing itself (core::ParseCifp) already is.

namespace trm::sdk {

// Custom Data/CIFP/<ICAO>.dat (a navdata-updater override, e.g. Navigraph)
// takes priority over Resources/default data/CIFP/<ICAO>.dat (X-Plane's own
// bundled data) -- same override convention real X-Plane itself uses for
// CIFP. nullopt if neither exists: plenty of smaller fields have no CIFP
// coverage at all, which the Flight Plan tab's Procedures section treats as
// "no candidates" rather than an error.
//
// Unlike sdk::AptDatLoader's one-time global load (apt.dat covers every
// airport in one file), CIFP is one file per airport -- this is called on
// demand whenever the origin/destination ICAO changes, not at startup.
std::optional<core::CifpProcedures> LoadCifpForAirport(const std::string& icao);

} // namespace trm::sdk
