# TrafficRunwayMonitor -- native X-Plane plugin
#
# `trm_core` is the pure-logic library -- zero XPLM/SDK dependency, fully
# unit-tested via tests/.

cmake_minimum_required(VERSION 3.16)

project(TrafficRunwayMonitor DESCRIPTION "Traffic Runway Monitor")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# trm_core is a static library linked into TrafficRunwayMonitor, which is
# itself built as a shared object (.xpl) on every platform -- without PIC,
# linking the static lib into a shared object fails on Linux with
# "relocation ... can not be used when making a shared object; recompile
# with -fPIC". Harmless on Windows/macOS, so set it unconditionally.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(SDK_DIR "${CMAKE_SOURCE_DIR}/third_party/SDK")
set(LTAPI_DIR "${CMAKE_SOURCE_DIR}/third_party/LTAPI")
set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/ImGui")
set(IMGWINDOW_DIR "${CMAKE_SOURCE_DIR}/third_party/ImgWindow")

include_directories(
    "${SDK_DIR}/CHeaders/XPLM"
    "${SDK_DIR}/CHeaders/Wrappers"
    "${LTAPI_DIR}"
    "${IMGUI_DIR}"
    "${IMGWINDOW_DIR}"
)

# Windows' SDK ships GL/gl.h (GL 1.1) but no GL/glext.h -- SystemGL.h
# (third_party/ImgWindow) includes <GL/glext.h> unconditionally on
# non-Apple platforms. See third_party/GL-shim/GL/glext.h's own comment:
# everything actually used is legacy GL 1.1, already in GL/gl.h, so an
# empty stub is sufficient here -- appended last so it never shadows a
# real glext.h on platforms that do have one.
if(WIN32)
    include_directories("${CMAKE_SOURCE_DIR}/third_party/GL-shim")
endif()

if(WIN32)
    list(APPEND CMAKE_LIBRARY_PATH "${SDK_DIR}/Libraries/Win")
endif()

# Enable X-Plane SDK APIs through the version this SDK download (4.3.0) provides.
add_definitions(
    -DXPLM200=1 -DXPLM210=1 -DXPLM300=1 -DXPLM301=1 -DXPLM302=1 -DXPLM303=1
    -DXPLM400=1 -DXPLM410=1 -DXPLM420=1 -DXPLM430=1
)
add_definitions(-DAPL=$<BOOL:${APPLE}> -DIBM=$<BOOL:${WIN32}> -DLIN=$<AND:$<BOOL:${UNIX}>,$<NOT:$<BOOL:${APPLE}>>>)

if(MSVC)
    add_compile_options(/wd4996 /wd4068)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(-Wall -Wextra -fexceptions -fvisibility=hidden)
endif()

add_library(TrafficRunwayMonitor SHARED
    src/Plugin.cpp
    src/sdk/AptDatLoader.cpp
    src/sdk/TcasSource.cpp
    src/sdk/LtapiSource.cpp
    src/sdk/FmsOrigin.cpp
    src/sdk/TerrainProbe.cpp
    src/sdk/Weather.cpp
    src/sdk/Log.cpp
    src/sdk/SettingsStore.cpp
    src/sdk/PluginPath.cpp
    src/ui/MainWindow.cpp
    src/ui/Widgets.cpp
    src/ui/Theme.cpp
    third_party/LTAPI/LTAPI.cpp
    third_party/ImGui/imgui.cpp
    third_party/ImGui/imgui_draw.cpp
    third_party/ImGui/imgui_widgets.cpp
    third_party/ImgWindow/ImgWindow.cpp
    third_party/ImgWindow/ImgFontAtlas.cpp
)
target_include_directories(TrafficRunwayMonitor PRIVATE "${CMAKE_SOURCE_DIR}/src")

if(WIN32)
    find_library(XPLM_LIBRARY NAMES XPLM XPLM_64.lib)
    target_link_libraries(TrafficRunwayMonitor ${XPLM_LIBRARY})
endif()

if(UNIX AND NOT APPLE)
    find_library(DL_LIBRARY dl)
    target_link_libraries(TrafficRunwayMonitor ${DL_LIBRARY})
endif()

if(APPLE)
    find_library(CORE_FOUNDATION_LIBRARY CoreFoundation)
    find_library(APPLICATION_SERVICES_LIBRARY ApplicationServices)
    # The vendored XPLM.framework isn't in any default search location, so
    # -framework XPLM alone can't find it -- needs an explicit -F path too.
    # ApplicationServices is required by ImGui's own OSX clipboard functions
    # (IMGUI_ENABLE_OSX_DEFAULT_CLIPBOARD_FUNCTIONS in imconfig.h) -- see
    # imgui.cpp's own comment on GetClipboardTextFn_DefaultImpl.
    target_link_libraries(TrafficRunwayMonitor "-F${SDK_DIR}/Libraries/Mac" "-framework XPLM"
                            ${CORE_FOUNDATION_LIBRARY} ${APPLICATION_SERVICES_LIBRARY})
endif()

# ui/MainWindow's ImGui + ImgWindow dependency: the vendored
# ImGui v1.78 WIP + ImgWindow snapshot from TwinFan/LiveTraffic -- see
# ui/MainWindow.h's comment for why this exact pairing, not latest ImGui.
if(WIN32)
    target_link_libraries(TrafficRunwayMonitor opengl32)
elseif(APPLE)
    target_link_libraries(TrafficRunwayMonitor "-framework OpenGL")
elseif(UNIX)
    find_package(OpenGL REQUIRED)
    target_link_libraries(TrafficRunwayMonitor OpenGL::GL)
endif()

if(WIN32)
    if(MINGW)
        target_link_libraries(TrafficRunwayMonitor -static-libgcc -static-libstdc++ -static)
    endif()
    # Multi-config generators (Visual Studio) append a per-config subfolder
    # to *_OUTPUT_DIRECTORY unless the per-config property is also set --
    # pin every config to the same win_x64/ folder X-Plane expects.
    foreach(OUTDIR_PROP RUNTIME_OUTPUT_DIRECTORY LIBRARY_OUTPUT_DIRECTORY)
        set_target_properties(TrafficRunwayMonitor PROPERTIES ${OUTDIR_PROP} "${CMAKE_BINARY_DIR}/win_x64")
        foreach(CFG Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER ${CFG} CFG_UPPER)
            set_target_properties(TrafficRunwayMonitor PROPERTIES ${OUTDIR_PROP}_${CFG_UPPER} "${CMAKE_BINARY_DIR}/win_x64")
        endforeach()
    endforeach()
elseif(APPLE)
    set_target_properties(TrafficRunwayMonitor PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mac_x64")
elseif(UNIX)
    set_target_properties(TrafficRunwayMonitor PROPERTIES LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lin_x64")
endif()

set_target_properties(TrafficRunwayMonitor PROPERTIES
    PREFIX ""
    OUTPUT_NAME "TrafficRunwayMonitor"
    SUFFIX ".xpl"
)

# Bundled icon font (ui/MainWindow::InitFontAtlas merges it into the shared
# atlas). Deployed as Resources/fonts/, a sibling of win_x64/, matching what
# sdk::PluginPath expects.
add_custom_command(TARGET TrafficRunwayMonitor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Resources/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/FontAwesome/fa-solid-900.ttf"
        "${CMAKE_BINARY_DIR}/Resources/fonts/trm_icons.ttf"
)

# --- core/: pure logic, no XPLM/SDK dependency (section 3) ---

add_library(trm_core STATIC
    src/core/GeoMath.cpp
    src/core/AglResolver.cpp
    src/core/AptDat.cpp
    src/core/ScenerypacksIni.cpp
    src/core/RunwayMatcher.cpp
    src/core/SlotValidity.cpp
    src/core/TrendFilter.cpp
    src/core/PhaseClassifier.cpp
    src/core/SightingTracker.cpp
    src/core/WindEstimate.cpp
    src/core/Aggregator.cpp
    src/core/FmsOrigin.cpp
    src/core/Weather.cpp
    src/core/Format.cpp
    src/core/SlotAssignment.cpp
    src/core/EventLog.cpp
    src/core/AdvisoryFormat.cpp
)
target_include_directories(trm_core PUBLIC "${CMAKE_SOURCE_DIR}/src")
target_compile_features(trm_core PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(trm_core PRIVATE /W4)
else()
    target_compile_options(trm_core PRIVATE -Wall -Wextra)
endif()

# sdk/TcasSource (real XPLM glue) depends on core::SlotValidity -- link
# trm_core in for its headers/symbols now that both targets exist. Plain
# (non-keyword) signature to match the earlier target_link_libraries calls
# on this same target above.
target_link_libraries(TrafficRunwayMonitor trm_core)

option(TRM_BUILD_TESTS "Build the Catch2 unit test suite for core/" ON)
if(TRM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
