#include "sdk/SettingsStore.h"

#include <cstdlib>
#include <fstream>
#include <string>

#include "XPLMUtilities.h"

#include "sdk/Log.h"

namespace trm::sdk {

namespace {

// XPLMGetPrefsPath returns X-Plane's own prefs file path (e.g. ".../Output/
// preferences/X-Plane.prf"); the SDK docs' documented convention is for a
// plugin to swap the filename for its own rather than write to that file
// directly.
std::string SettingsFilePath()
{
    char buf[512];
    XPLMGetPrefsPath(buf);
    const std::string path(buf);
    const std::string::size_type sepPos = path.find_last_of("\\/");
    const std::string dir = (sepPos == std::string::npos) ? std::string() : path.substr(0, sepPos + 1);
    return dir + "TrafficRunwayMonitor.prf";
}

} // namespace

void SaveSettings(const PersistedSettings& settings)
{
    std::ofstream out(SettingsFilePath(), std::ios::trunc);
    if (!out.is_open()) {
        Log(LogLevel::Warn, "could not write settings file, changes won't persist across restarts.");
        return;
    }
    out << "search_radius_nm=" << settings.search_radius_nm << '\n';
    out << "max_displayed_airports=" << settings.max_displayed_airports << '\n';
    out << "active_window_min=" << settings.active_window_min << '\n';
    out << "text_size_scale=" << settings.text_size_scale << '\n';
    out << "show_raw_metar=" << (settings.show_raw_metar ? 1 : 0) << '\n';
    out << "debug_log_runway_matches=" << (settings.debug_log_runway_matches ? 1 : 0) << '\n';
    out << "pressure_unit=" << settings.pressure_unit << '\n';
}

std::optional<PersistedSettings> LoadSettings()
{
    std::ifstream in(SettingsFilePath());
    if (!in.is_open()) {
        return std::nullopt;
    }

    PersistedSettings settings;
    std::string line;
    while (std::getline(in, line)) {
        const std::string::size_type eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);

        if (key == "search_radius_nm") {
            settings.search_radius_nm = std::atoi(value.c_str());
        } else if (key == "max_displayed_airports") {
            settings.max_displayed_airports = std::atoi(value.c_str());
        } else if (key == "active_window_min") {
            settings.active_window_min = std::atoi(value.c_str());
        } else if (key == "text_size_scale") {
            settings.text_size_scale = std::strtof(value.c_str(), nullptr);
        } else if (key == "show_raw_metar") {
            settings.show_raw_metar = (value == "1");
        } else if (key == "debug_log_runway_matches") {
            settings.debug_log_runway_matches = (value == "1");
        } else if (key == "pressure_unit") {
            settings.pressure_unit = std::atoi(value.c_str());
        }
    }
    return settings;
}

} // namespace trm::sdk
