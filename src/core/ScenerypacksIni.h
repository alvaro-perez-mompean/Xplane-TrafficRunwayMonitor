#pragma once

#include <istream>
#include <string>
#include <vector>

// Parses X-Plane's Custom Scenery/scenery_packs.ini -- the file that
// records which scenery packs are enabled and in what priority order
// (earlier entries outrank later ones).
//
// Pure logic, no filesystem access: ParseScenerypacksIni takes an
// already-open stream, same split as core::ParseAptDat / sdk::AptDatLoader.
// Locating the real file is sdk/AptDatLoader's job, not this module's.

namespace trm::core {

struct ScenerypacksEntry {
    // Pack path exactly as written in the file (e.g.
    // "Custom Scenery/KJFK Airport/"), relative to the X-Plane root unless
    // it's an absolute path (observed in the wild for externally-stored
    // packs, e.g. Ortho4XP overlays on another drive). Empty when
    // is_global_airports_marker is true.
    std::string path;

    // True for the special "SCENERY_PACK *GLOBAL_AIRPORTS*" entry, which
    // marks where X-Plane's own default global scenery slots into the
    // priority order relative to the surrounding custom packs -- it is not
    // itself a filesystem path.
    bool is_global_airports_marker = false;
};

// Enabled packs only, in the file's own priority order (first = highest
// priority). SCENERY_PACK_DISABLED lines are skipped entirely: a disabled
// pack contributes nothing, so it can't affect priority ordering either.
std::vector<ScenerypacksEntry> ParseScenerypacksIni(std::istream& in);

// True if `path` (a ScenerypacksEntry::path value) is already absolute and
// must be used as-is rather than resolved relative to the X-Plane root --
// observed in the wild for a pack stored on a different drive entirely (an
// Ortho4XP overlay at "H:\Ortho4XP\...").
bool IsAbsolutePath(const std::string& path);

} // namespace trm::core
