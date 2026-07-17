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

// Pascals-to-display-unit conversion factors, shared by FormatAltimeter
// below and by core::AdvisoryFormat's spoken/plain-text phraseology
// (which needs the same conversion but without FormatAltimeter's
// unit-name suffix).
constexpr double kPaToInHg = 0.0002953;
constexpr double kPaToHpa = 0.01;

// Altimeter setting readout, e.g. FormatAltimeter(101325.0, kInHg) ==
// "29.92 inHg", FormatAltimeter(101325.0, kHpa) == "1013 hPa". `pressurePa`
// is station pressure in Pascals (XPLMWeatherInfo_t's pressure_alt field).
std::string FormatAltimeter(double pressurePa, PressureUnit unit);

} // namespace trm::core
