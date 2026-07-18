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
