#pragma once

#include <optional>
#include <string>

#include "XPLMDataAccess.h"

#include "core/WindEstimate.h"

// Real XPLM SDK glue for weather queries: per-airport wind via
// XPLMGetWeatherAtLocation, the sim/weather/region/ array fallback, and
// METAR text via XPLMGetMETARForAirport.
//
// XPLMGetWeatherAtLocation and XPLMGetMETARForAirport are always callable
// once linked against this SDK, so no availability check is needed for
// them. The region-array fallback datarefs genuinely may or may not be
// registered at runtime, though, so that check is kept, same as
// sdk::TcasSource's HasExtendedTraffic().

namespace trm::sdk {

class Weather {
public:
    Weather();

    bool HasWindDatarefs() const { return has_wind_datarefs_; }

    // Wind at a specific position/elevation. XPLMGetWeatherAtLocation's own
    // docs guarantee the struct always contains the best data available
    // (the int return is has_station_match, not an availability signal),
    // so this always returns a real reading, never nullopt.
    core::WindReading QueryWindAt(double latDeg, double lonDeg, double altM) const;

    // Region-array fallback (the sim/weather/region/ 13-layer arrays) --
    // used only when the caller has already decided QueryWindAt isn't
    // applicable this cycle (e.g. no valid airport position to query at).
    // nullopt if the fallback datarefs aren't registered.
    std::optional<core::WindReading> ReadRegionWindFallback() const;

    // Last-downloaded METAR text for icao, or nullopt if empty/unavailable.
    std::optional<std::string> GetMetarFor(const std::string& icao) const;

private:
    bool has_wind_datarefs_ = false;
    XPLMDataRef wind_region_speed_ref_ = nullptr;
    XPLMDataRef wind_region_dir_ref_ = nullptr;
    XPLMDataRef wind_region_alt_ref_ = nullptr;
};

} // namespace trm::sdk
