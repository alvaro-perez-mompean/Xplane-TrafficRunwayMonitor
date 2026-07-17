# Catch2 unit tests for core/ -- pure logic, no X-Plane SDK dependency.

include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(Catch2)

add_executable(trm_core_tests
    test_GeoMath.cpp
    test_AptDat.cpp
    test_ScenerypacksIni.cpp
    test_RunwayMatcher.cpp
    test_SlotValidity.cpp
    test_TrendFilter.cpp
    test_PhaseClassifier.cpp
    test_SightingTracker.cpp
    test_WindEstimate.cpp
    test_Aggregator.cpp
    test_FmsOrigin.cpp
    test_Weather.cpp
    test_Format.cpp
    test_SlotAssignment.cpp
    test_EventLog.cpp
)
target_link_libraries(trm_core_tests PRIVATE trm_core Catch2::Catch2WithMain)
target_compile_features(trm_core_tests PRIVATE cxx_std_17)

list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
include(CTest)
include(Catch)
catch_discover_tests(trm_core_tests)
