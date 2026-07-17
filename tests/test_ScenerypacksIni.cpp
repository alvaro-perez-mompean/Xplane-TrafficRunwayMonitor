#include <catch2/catch_test_macros.hpp>

#include <sstream>

#include "core/ScenerypacksIni.h"

using namespace trm::core;

TEST_CASE("ParseScenerypacksIni: reads enabled packs in file order", "[ScenerypacksIni]")
{
    std::istringstream in(
        "I\n"
        "1000 Version\n"
        "SCENERY\n"
        "\n"
        "SCENERY_PACK Custom Scenery/KJFK Airport/\n"
        "SCENERY_PACK Custom Scenery/KLAX Airport/\n");

    const auto entries = ParseScenerypacksIni(in);

    REQUIRE(entries.size() == 2);
    CHECK(entries[0].path == "Custom Scenery/KJFK Airport/");
    CHECK_FALSE(entries[0].is_global_airports_marker);
    CHECK(entries[1].path == "Custom Scenery/KLAX Airport/");
}

TEST_CASE("ParseScenerypacksIni: skips SCENERY_PACK_DISABLED lines entirely", "[ScenerypacksIni]")
{
    std::istringstream in(
        "SCENERY_PACK Custom Scenery/Enabled Pack/\n"
        "SCENERY_PACK_DISABLED Custom Scenery/Disabled Pack/\n");

    const auto entries = ParseScenerypacksIni(in);

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].path == "Custom Scenery/Enabled Pack/");
}

TEST_CASE("ParseScenerypacksIni: *GLOBAL_AIRPORTS* becomes a marker entry, not a path", "[ScenerypacksIni]")
{
    std::istringstream in(
        "SCENERY_PACK Custom Scenery/Above/\n"
        "SCENERY_PACK *GLOBAL_AIRPORTS*\n"
        "SCENERY_PACK Custom Scenery/Below/\n");

    const auto entries = ParseScenerypacksIni(in);

    REQUIRE(entries.size() == 3);
    CHECK_FALSE(entries[0].is_global_airports_marker);
    CHECK(entries[1].is_global_airports_marker);
    CHECK(entries[1].path.empty());
    CHECK_FALSE(entries[2].is_global_airports_marker);
}

TEST_CASE("ParseScenerypacksIni: tolerates trailing carriage returns (CRLF files)", "[ScenerypacksIni]")
{
    std::istringstream in("SCENERY_PACK Custom Scenery/KJFK Airport/\r\n");

    const auto entries = ParseScenerypacksIni(in);

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].path == "Custom Scenery/KJFK Airport/");
}

TEST_CASE("ParseScenerypacksIni: absolute paths pass through unchanged", "[ScenerypacksIni]")
{
    std::istringstream in("SCENERY_PACK H:\\Ortho4XP\\_internal\\Ortho4XP_Data\\yOrtho4XP_Overlays/\n");

    const auto entries = ParseScenerypacksIni(in);

    REQUIRE(entries.size() == 1);
    CHECK(entries[0].path == "H:\\Ortho4XP\\_internal\\Ortho4XP_Data\\yOrtho4XP_Overlays/");
}

TEST_CASE("ParseScenerypacksIni: empty input yields no entries", "[ScenerypacksIni]")
{
    std::istringstream in("");
    CHECK(ParseScenerypacksIni(in).empty());
}
