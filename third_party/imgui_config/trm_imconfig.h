// Project-owned Dear ImGui compile-time config (IMGUI_USER_CONFIG), kept
// outside third_party/ImGui since that directory is now a submodule of
// upstream ocornut/imgui and can't carry local edits. Wired in via
// CMakeLists.txt's IMGUI_USER_CONFIG define; imgui.h includes this file
// and then its own (stock, all-commented-out) imconfig.h unconditionally,
// so there's nothing in the submodule's copy left to conflict with.

#pragma once

#include <cassert>

#if defined(__APPLE__) && !defined(TARGET_OS_OSX)
#define TARGET_OS_OSX 1
#endif

// IM_ASSERT hook: logs to X-Plane's Log.txt before asserting. Definitions
// originally copied from LiveTraffic's Include/TextIO.h so this config
// doesn't need to include LiveTraffic's own header stack; LogFatalMsg is
// defined in ui/MainWindow.cpp.
void LogFatalMsg(const char* szPath, int ln, const char* szFunc, const char* szMsg, ...);
#define IM_ASSERT(_EXPR)                                                                \
    do {                                                                               \
        if (!(_EXPR)) {                                                                \
            LogFatalMsg(__FILE__, __LINE__, __func__, "ImGui ASSERT FAILED: %s", #_EXPR); \
            assert(_EXPR);                                                             \
        }                                                                              \
    } while (0)

#define IMGUI_DISABLE_OBSOLETE_FUNCTIONS
#define IMGUI_DISABLE_DEMO_WINDOWS
#define IMGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS
