#include "sdk/CifpLoader.h"

#include <fstream>

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
// here (same helper shape as sdk::AptDatLoader's own XPlaneRoot).
std::string XPlaneRoot()
{
    char buf[512];
    XPLMGetSystemPath(buf);
    return std::string(buf);
}

} // namespace

std::optional<core::CifpProcedures> LoadCifpForAirport(const std::string& icao)
{
    const std::string root = XPlaneRoot();
    const std::string customPath = root + "Custom Data" + kSep + "CIFP" + kSep + icao + ".dat";
    const std::string defaultPath =
        root + "Resources" + kSep + "default data" + kSep + "CIFP" + kSep + icao + ".dat";

    for (const std::string& path : {customPath, defaultPath}) {
        std::ifstream in(path);
        if (in.is_open()) {
            return core::ParseCifp(in);
        }
    }
    return std::nullopt;
}

} // namespace trm::sdk
