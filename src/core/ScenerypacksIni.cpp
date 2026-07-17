#include "core/ScenerypacksIni.h"

namespace trm::core {

namespace {

constexpr const char* kPackPrefix = "SCENERY_PACK "; // trailing space distinguishes this from SCENERY_PACK_DISABLED
constexpr const char* kGlobalAirportsMarker = "*GLOBAL_AIRPORTS*";

std::string TrimTrailingWhitespace(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) {
        s.pop_back();
    }
    return s;
}

} // namespace

std::vector<ScenerypacksEntry> ParseScenerypacksIni(std::istream& in)
{
    std::vector<ScenerypacksEntry> entries;

    std::string line;
    while (std::getline(in, line)) {
        line = TrimTrailingWhitespace(line);
        if (line.rfind(kPackPrefix, 0) != 0) {
            continue; // not a SCENERY_PACK line (also excludes SCENERY_PACK_DISABLED, which has no space here)
        }

        std::string path = line.substr(std::string(kPackPrefix).size());
        ScenerypacksEntry entry;
        if (path == kGlobalAirportsMarker) {
            entry.is_global_airports_marker = true;
        } else {
            entry.path = std::move(path);
        }
        entries.push_back(std::move(entry));
    }

    return entries;
}

bool IsAbsolutePath(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    if (path.front() == '/' || path.front() == '\\') {
        return true;
    }
    return path.size() >= 2 && path[1] == ':'; // drive letter, e.g. "H:\..."
}

} // namespace trm::core
