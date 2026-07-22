#include "sdk/Weather.h"
#include "core/Weather.h"

#include "XPLMWeather.h"

#include <array>
#include <cstring>

namespace trm::sdk {

namespace {
constexpr double kMpsToKt = 1.9438444924406;
constexpr int kWindRegionLayerCount = 13; // confirmed array size in X-Plane 12
constexpr double kMetersToSm = 1.0 / 1609.344;
constexpr double kMetersToFt = 3.28084;
// XPLMWeatherInfo_t has no ceiling field, only cloud layers with a 0..1
// coverage ratio. "Broken" (5/8 okta) is the traditional METAR threshold for a
// reportable ceiling -- scattered and few don't count. Not aeronautically
// researched beyond that convention.
constexpr float kCeilingCoverageThreshold = 0.625f;
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

    // Both of these feed apt.dat flow rules 1002/1003. Anything we can't
    // measure is left at the struct's unrestricted default rather than 0, so a
    // missing reading can never wrongly disqualify a flow.
    if (info.visibility > 0.0f) {
        reading.visibility_sm = info.visibility * kMetersToSm;
    }
    // XPLMWeatherInfoClouds_t::alt_base is documented MSL, but apt.dat row 1002
    // is a ceiling ABOVE FIELD ELEVATION, so the layer base is reported
    // relative to the altitude this query was made at. Callers wanting a
    // ceiling for flow rules must therefore query at field elevation, which
    // Plugin.cpp does. Getting this wrong is invisible at a sea-level airport
    // and badly wrong at a high one: at KDEN (5,434ft) an overcast layer 400ft
    // above the runway would otherwise read as a 5,900ft ceiling and clear any
    // low-visibility gate.
    // "Found a layer" is tracked separately rather than by a negative sentinel:
    // subtracting the query altitude means a real layer at or below field
    // elevation legitimately produces a negative height, which a sentinel would
    // swallow as "no ceiling" and leave unrestricted -- the exact inversion of
    // what fog on the field means.
    bool haveCeiling = false;
    double lowestCeilingFt = 0.0;
    for (const auto& layer : info.cloud_layers) {
        if (layer.coverage < kCeilingCoverageThreshold) {
            continue;
        }
        const double baseFt = (layer.alt_base - altM) * kMetersToFt;
        if (!haveCeiling || baseFt < lowestCeilingFt) {
            lowestCeilingFt = baseFt;
            haveCeiling = true;
        }
    }
    if (haveCeiling) {
        // Clamped at 0 rather than left negative: "ceiling at or below the
        // field" fails every non-zero minimum either way, but a negative reads
        // as a bug in the log.
        reading.ceiling_ft = lowestCeilingFt < 0.0 ? 0.0 : lowestCeilingFt;
    }

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
