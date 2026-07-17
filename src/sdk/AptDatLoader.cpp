#include "sdk/AptDatLoader.h"

#include <fstream>
#include <string>
#include <vector>

#include "core/ScenerypacksIni.h"
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

std::optional<core::AirportDatabase> LoadDefaultAptDatFrom(const std::string& root)
{
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

} // namespace

std::optional<core::AirportDatabase> LoadDefaultAptDat()
{
    return LoadDefaultAptDatFrom(XPlaneRoot());
}

std::optional<core::AirportDatabase> LoadMergedAptDat()
{
    const std::string root = XPlaneRoot();
    const std::optional<core::AirportDatabase> defaultDb = LoadDefaultAptDatFrom(root);

    std::ifstream iniStream(root + "Custom Scenery" + kSep + "scenery_packs.ini");
    if (!iniStream.is_open()) {
        return defaultDb; // no scenery_packs.ini to consult -- fall back to the default database alone
    }
    const std::vector<core::ScenerypacksEntry> entries = core::ParseScenerypacksIni(iniStream);

    // Stable storage for each custom pack's parsed database: orderedDbs
    // below holds pointers into this, so it must outlive the merge call.
    // reserve() up front (entries.size() is an upper bound, since only
    // packs that actually ship an apt.dat get pushed) keeps those pointers
    // valid by ruling out reallocation.
    std::vector<core::AirportDatabase> customDbs;
    customDbs.reserve(entries.size());

    std::vector<const core::AirportDatabase*> orderedDbs;
    bool sawGlobalAirportsMarker = false;

    for (const core::ScenerypacksEntry& entry : entries) {
        if (entry.is_global_airports_marker) {
            if (!sawGlobalAirportsMarker && defaultDb) {
                orderedDbs.push_back(&*defaultDb);
            }
            sawGlobalAirportsMarker = true;
            continue;
        }

        std::string base = core::IsAbsolutePath(entry.path) ? entry.path : root + entry.path;
        if (!base.empty() && base.back() != '/' && base.back() != '\\') {
            base += '/';
        }
        std::ifstream packStream(base + "Earth nav data/apt.dat");
        if (!packStream.is_open()) {
            continue; // most packs (textures/objects/landmarks) carry no apt.dat at all
        }
        customDbs.push_back(core::ParseAptDat(packStream));
        orderedDbs.push_back(&customDbs.back());
    }

    // scenery_packs.ini is expected to always contain the *GLOBAL_AIRPORTS*
    // marker, but if some non-standard install doesn't, still fall back to
    // consulting the default database last rather than dropping it silently.
    if (!sawGlobalAirportsMarker && defaultDb) {
        orderedDbs.push_back(&*defaultDb);
    }

    if (orderedDbs.empty()) {
        return defaultDb;
    }
    return core::MergeAirportDatabases(orderedDbs);
}

} // namespace trm::sdk
