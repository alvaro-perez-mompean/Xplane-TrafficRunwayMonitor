#pragma once

#include <optional>
#include <string>

// Locates files bundled with the plugin itself (as opposed to
// sdk::AptDatLoader's XPLMGetSystemPath calls, which locate files inside
// X-Plane's own install).
//
// Real XPLMGetPluginInfo call -- thin glue, not unit-tested.

namespace trm::sdk {

// Full path to Resources/fonts/trm_icons.ttf, a sibling of the platform
// output directory (win_x64/mac_x64/lin_x64) that CMake's
// add_custom_command (CMakeLists.txt) copies the bundled icon font into.
// nullopt if XPLMGetPluginInfo returned a file path in an unexpected shape
// (caller should skip merging the icon font rather than treat this as
// fatal -- see MainWindow::InitFontAtlas).
std::optional<std::string> IconFontPath();

} // namespace trm::sdk
