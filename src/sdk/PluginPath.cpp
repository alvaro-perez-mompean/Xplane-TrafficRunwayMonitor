#include "sdk/PluginPath.h"

#include "XPLMPlugin.h"

namespace trm::sdk {

namespace {

#if IBM
constexpr char kSep = '\\';
#else
constexpr char kSep = '/';
#endif

// XPLMGetPluginInfo's outFilePath is the absolute path to this plugin's own
// .xpl, e.g. ".../TrafficRunwayMonitor/win_x64/TrafficRunwayMonitor.xpl" --
// strip the filename and its per-platform output directory to land on the
// plugin's own root, the level Resources/ sits at (see CMakeLists.txt's
// matching add_custom_command).
std::optional<std::string> PluginRootDir()
{
    char buf[512];
    XPLMGetPluginInfo(XPLMGetMyID(), nullptr, buf, nullptr, nullptr);
    std::string filePath(buf);

    std::size_t lastSep = filePath.find_last_of("/\\");
    if (lastSep == std::string::npos || lastSep == 0) {
        return std::nullopt;
    }
    std::size_t secondLastSep = filePath.find_last_of("/\\", lastSep - 1);
    if (secondLastSep == std::string::npos) {
        return std::nullopt;
    }
    return filePath.substr(0, secondLastSep + 1);
}

} // namespace

std::optional<std::string> IconFontPath()
{
    std::optional<std::string> root = PluginRootDir();
    if (!root.has_value()) {
        return std::nullopt;
    }
    return *root + "Resources" + kSep + "fonts" + kSep + "trm_icons.ttf";
}

} // namespace trm::sdk
