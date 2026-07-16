#include "sdk/AptDatLoader.h"

#include <fstream>
#include <string>

#include "XPLMUtilities.h"

namespace trm::sdk {

namespace {

#if IBM
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

// XPLMGetSystemPath's own docs guarantee a trailing separator already, in
// native OS conventions -- no suffix-stripping/root-resolution hack needed
// here.
std::string XPlaneRoot()
{
    char buf[512];
    XPLMGetSystemPath(buf);
    return std::string(buf);
}

} // namespace

std::optional<core::AirportDatabase> LoadDefaultAptDat()
{
    const std::string root = XPlaneRoot();

    // X-Plane 12 moved the default global airport database out of
    // Resources: it's now at Global Scenery/Global Airports/Earth nav
    // data/apt.dat, the "one and only source" per X-Plane's own docs.
    // X-Plane 11 (and early XP12 betas) used the older Resources/default
    // scenery/default apt dat/ layout. Both paths are tried, XP12 first.
    const std::string xp12Path =
        root + "Global Scenery" + kSep + "Global Airports" + kSep + "Earth nav data" + kSep + "apt.dat";
    const std::string xp11Path = root + "Resources" + kSep + "default scenery" + kSep + "default apt dat" + kSep +
                                  "Earth nav data" + kSep + "apt.dat";

    for (const std::string& path : {xp12Path, xp11Path}) {
        std::ifstream in(path);
        if (in.is_open()) {
            return core::ParseAptDat(in);
        }
    }
    return std::nullopt;
}

} // namespace trm::sdk
