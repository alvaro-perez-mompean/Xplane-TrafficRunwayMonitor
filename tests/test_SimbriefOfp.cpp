#include <catch2/catch_test_macros.hpp>

#include "core/SimbriefOfp.h"

using namespace trm::core;

TEST_CASE("ParseSimbriefOfp: well-formed JSON extracts both ICAOs", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"KJFK","iata_code":"JFK"},"destination":{"icao_code":"KLAX","iata_code":"LAX"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    REQUIRE(result.destination_icao.has_value());
    CHECK(*result.origin_icao == "KJFK");
    CHECK(*result.destination_icao == "KLAX");
}

TEST_CASE("ParseSimbriefOfp: missing destination key yields nullopt for that field only", "[SimbriefOfp]")
{
    const std::string json = R"({"origin":{"icao_code":"KJFK"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    CHECK(*result.origin_icao == "KJFK");
    CHECK_FALSE(result.destination_icao.has_value());
}

TEST_CASE("ParseSimbriefOfp: reordered keys within the object still parse", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"iata_code":"JFK","name":"John F Kennedy","icao_code":"KJFK"},)"
        R"("destination":{"icao_code":"KLAX","name":"Los Angeles Intl"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    REQUIRE(result.destination_icao.has_value());
    CHECK(*result.origin_icao == "KJFK");
    CHECK(*result.destination_icao == "KLAX");
}

TEST_CASE("ParseSimbriefOfp: malformed/truncated JSON yields both nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefOfp("not json at all");
    CHECK_FALSE(result.origin_icao.has_value());
    CHECK_FALSE(result.destination_icao.has_value());
}

TEST_CASE("ParseSimbriefOfp: empty string yields both nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefOfp("");
    CHECK_FALSE(result.origin_icao.has_value());
    CHECK_FALSE(result.destination_icao.has_value());
}

TEST_CASE("ParseSimbriefOfp: assembles a LIDO-style route line with both planned runways", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":"20"},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL/20 SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: route line omits a runway suffix that hasn't been planned yet", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":""},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: route line is nullopt when general.route is missing", "[SimbriefOfp]")
{
    const std::string json = R"({"origin":{"icao_code":"LEBL"},"destination":{"icao_code":"LEMD"}})";
    const auto result = ParseSimbriefOfp(json);
    CHECK_FALSE(result.route_text.has_value());
}

TEST_CASE("ParseSimbriefOfp: route line is nullopt when an ICAO is missing even if route is present", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL"},"general":{"route":"SENIA2J SENIA Z596"}})";
    const auto result = ParseSimbriefOfp(json);
    CHECK_FALSE(result.route_text.has_value());
}

TEST_CASE("ParseSimbriefOfp: route survives an empty nested object ahead of it in general (real Simbrief shape)",
          "[SimbriefOfp]")
{
    // Simbrief's actual "general" object puts "sys_rmk":{} (and other empty
    // placeholder objects) before "route" -- a naive `[^}]*` object-body
    // capture stops at sys_rmk's own closing brace and never reaches route.
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":"20"},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"icao_airline":"IBE","sys_rmk":{},"route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D",)"
        R"("route_track_replace":{}}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL/20 SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: route survives multiple levels of nested objects and arrays ahead of it",
          "[SimbriefOfp]")
{
    // A non-empty, multi-level-deep nested object/array (not just Simbrief's
    // own "sys_rmk":{}) ahead of "route" -- proves the manual brace-depth
    // scan handles arbitrary nesting, not just the one level a prior regex
    // fix happened to tolerate.
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":"20"},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"nested":{"deeper":{"a":"b"},"list":[{"x":1},{"y":2}]},)"
        R"("route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL/20 SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: a brace character inside a quoted string doesn't confuse the object scan",
          "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","name":"literal brace } in a string \" with an escaped quote too"},)"
        R"("destination":{"icao_code":"LEMD"},)"
        R"("general":{"route":"SENIA2J SENIA Z596"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    CHECK(*result.origin_icao == "LEBL");
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL SENIA2J SENIA Z596 LEMD");
}
