#include <catch2/catch_test_macros.hpp>

#include "core/FmsOrigin.h"

using namespace trm::core;

TEST_CASE("ResolveNativeFmsOriginDestination: no flight plan yields both nullopt", "[FmsOrigin]")
{
    const auto result = ResolveNativeFmsOriginDestination(0, FmsEntryInfo{true, "KJFK"}, FmsEntryInfo{true, "KLAX"});
    CHECK_FALSE(result.origin_icao.has_value());
    CHECK_FALSE(result.destination_icao.has_value());
}

TEST_CASE("ResolveNativeFmsOriginDestination: both airport entries resolve", "[FmsOrigin]")
{
    const auto result = ResolveNativeFmsOriginDestination(4, FmsEntryInfo{true, "KJFK"}, FmsEntryInfo{true, "KLAX"});
    REQUIRE(result.origin_icao.has_value());
    REQUIRE(result.destination_icao.has_value());
    CHECK(*result.origin_icao == "KJFK");
    CHECK(*result.destination_icao == "KLAX");
}

TEST_CASE("ResolveNativeFmsOriginDestination: non-airport entries are ignored", "[FmsOrigin]")
{
    const auto result = ResolveNativeFmsOriginDestination(4, FmsEntryInfo{false, "KJFK"}, FmsEntryInfo{true, "KLAX"});
    CHECK_FALSE(result.origin_icao.has_value());
    REQUIRE(result.destination_icao.has_value());
    CHECK(*result.destination_icao == "KLAX");
}

TEST_CASE("ResolveNativeFmsOriginDestination: airport entries with an empty id are ignored", "[FmsOrigin]")
{
    const auto result = ResolveNativeFmsOriginDestination(4, FmsEntryInfo{true, ""}, FmsEntryInfo{true, "KLAX"});
    CHECK_FALSE(result.origin_icao.has_value());
    REQUIRE(result.destination_icao.has_value());
}

TEST_CASE("ResolveEffectiveIcao: fresh always wins, even over a standing override", "[FmsOrigin]")
{
    const auto result = ResolveEffectiveIcao(true, std::string("KJFK"), std::string("KLAX"));
    REQUIRE(result.has_value());
    CHECK(*result == "KJFK");
}

TEST_CASE("ResolveEffectiveIcao: stale with an override set uses the override", "[FmsOrigin]")
{
    const auto result = ResolveEffectiveIcao(false, std::nullopt, std::string("KLAX"));
    REQUIRE(result.has_value());
    CHECK(*result == "KLAX");
}

TEST_CASE("ResolveEffectiveIcao: stale with no override set yields nullopt", "[FmsOrigin]")
{
    CHECK_FALSE(ResolveEffectiveIcao(false, std::nullopt, std::nullopt).has_value());
}
