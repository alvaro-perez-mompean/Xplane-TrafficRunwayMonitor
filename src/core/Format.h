#pragma once

#include <string>

// Small display-formatting helpers with zero UI/XPLM dependency, pulled out
// of ui/MainWindow specifically so they stay unit-tested.

namespace trm::core {

// "how long ago" readout: whole seconds under a minute, whole minutes from
// a minute up (matches ACTIVE_WINDOW_SEC's minutes-scale granularity -- no
// one needs "3600s ago"). e.g. FormatAgo(45.0) == "45s ago",
// FormatAgo(125.0) == "2m ago".
std::string FormatAgo(double elapsedSec);

// User-selectable display unit for altimeter/QNH readings (Settings tab).
enum class PressureUnit { kInHg, kHpa };

// Altimeter setting readout, e.g. FormatAltimeter(101325.0, kInHg) ==
// "29.92 inHg", FormatAltimeter(101325.0, kHpa) == "1013 hPa". `pressurePa`
// is station pressure in Pascals (XPLMWeatherInfo_t's pressure_alt field).
std::string FormatAltimeter(double pressurePa, PressureUnit unit);

} // namespace trm::core
