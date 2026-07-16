#pragma once

#include <string>

// Thin wrapper over XPLMDebugString (visible in X-Plane's Log.txt), adding
// a consistent "TrafficRunwayMonitor: [LEVEL] " prefix and trailing newline
// so every line this plugin emits is easy to spot and grep for -- replaces
// the ad-hoc XPLMDebugString calls that used to duplicate this formatting
// at each call site.
//
// Real XPLM call -- thin glue, not unit-tested, same as the rest of sdk/.

namespace trm::sdk {

enum class LogLevel { Info, Warn, Error };

void Log(LogLevel level, const std::string& message);

} // namespace trm::sdk
