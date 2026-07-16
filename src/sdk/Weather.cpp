#include "sdk/Weather.h"
#include "core/Weather.h"

#include "XPLMWeather.h"

#include <array>
#include <cstring>

namespace trm::sdk {

namespace {
constexpr double kMpsToKt = 1.9438444924406;
constexpr int kWindRegionLayerCount = 13; // confirmed array size in X-Plane 12
} // namespace

Weather::Weather()
{
    wind_region_speed_ref_ = XPLMFindDataRef("sim/weather/region/wind_speed_msc");
    wind_region_dir_ref_ = XPLMFindDataRef("sim/weather/region/wind_direction_degt");
    wind_region_alt_ref_ = XPLMFindDataRef("sim/weather/region/wind_altitude_msl_m");
    has_wind_datarefs_ = wind_region_speed_ref_ && wind_region_dir_ref_ && wind_region_alt_ref_;
}

core::WindReading Weather::QueryWindAt(double latDeg, double lonDeg, double altM) const
{
    XPLMWeatherInfo_t info;
    std::memset(&info, 0, sizeof(info));
    info.structSize = sizeof(XPLMWeatherInfo_t);
    const int foundStationData = XPLMGetWeatherAtLocation(latDeg, lonDeg, altM, &info);

    core::WindReading reading;
    reading.speed_kt = info.wind_spd_alt * kMpsToKt; // wind_spd_alt is documented in meters/sec
    reading.direction_true_deg = info.wind_dir_alt;
    reading.has_station_match = (foundStationData == 1);
    reading.pressure_pa = info.pressure_alt;
    return reading;
}

std::optional<core::WindReading> Weather::ReadRegionWindFallback() const
{
    if (!has_wind_datarefs_) {
        return std::nullopt;
    }

    std::array<float, kWindRegionLayerCount> alt{};
    std::array<float, kWindRegionLayerCount> speed{};
    std::array<float, kWindRegionLayerCount> dir{};
    XPLMGetDatavf(wind_region_alt_ref_, alt.data(), 0, kWindRegionLayerCount);
    XPLMGetDatavf(wind_region_speed_ref_, speed.data(), 0, kWindRegionLayerCount);
    XPLMGetDatavf(wind_region_dir_ref_, dir.data(), 0, kWindRegionLayerCount);

    std::vector<core::WindLayer> layers;
    layers.reserve(kWindRegionLayerCount);
    for (int i = 0; i < kWindRegionLayerCount; ++i) {
        layers.push_back(core::WindLayer{alt[i], speed[i], dir[i]});
    }

    const auto lowest = core::PickLowestAltitudeWindLayer(layers);
    if (!lowest) {
        return std::nullopt;
    }

    core::WindReading reading;
    // X-Plane's own dataref description claims knots for wind_speed_msc
    // despite the "_msc" suffix (X-Plane's usual meters/second marker
    // elsewhere) -- confirmed in-sim against real conditions, so no unit
    // conversion here.
    reading.speed_kt = lowest->speed_kt;
    reading.direction_true_deg = lowest->direction_true_deg;
    reading.has_station_match = false; // region fallback is never a real station match
    return reading;
}

std::optional<std::string> Weather::GetMetarFor(const std::string& icao) const
{
    XPLMFixedString150_t buf;
    std::memset(&buf, 0, sizeof(buf));
    XPLMGetMETARForAirport(icao.c_str(), &buf);

    const std::string text(buf.buffer);
    if (text.empty()) {
        return std::nullopt;
    }
    return text;
}

} // namespace trm::sdk
