#include "sdk/Log.h"

#include "XPLMUtilities.h"

namespace trm::sdk {

namespace {

const char* LevelLabel(LogLevel level)
{
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "INFO";
}

} // namespace

void Log(LogLevel level, const std::string& message)
{
    const std::string line = "TrafficRunwayMonitor: [" + std::string(LevelLabel(level)) + "] " + message + "\n";
    XPLMDebugString(line.c_str());
}

} // namespace trm::sdk
