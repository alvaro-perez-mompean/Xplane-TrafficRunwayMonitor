#include <catch2/catch_test_macros.hpp>

#include "core/FmsOrigin.h"

using namespace trm::core;

TEST_CASE("ParseToLissInitPageFromTo: valid INIT-page snapshot extracts FROM/TO", "[FmsOrigin]")
{
    const ToLissMcduSnapshot snapshot{"INIT", "CO RTE    FROM/TO", "BIOGRO01        LEBB/LEGE"};
    const auto result = ParseToLissInitPageFromTo(snapshot);
    REQUIRE(result.has_value());
    CHECK(result->first == "LEBB");
    CHECK(result->second == "LEGE");
}

TEST_CASE("ParseToLissInitPageFromTo: requires INIT in the title", "[FmsOrigin]")
{
    const ToLissMcduSnapshot snapshot{"FPLN", "CO RTE    FROM/TO", "BIOGRO01        LEBB/LEGE"};
    CHECK_FALSE(ParseToLissInitPageFromTo(snapshot).has_value());
}

TEST_CASE("ParseToLissInitPageFromTo: requires FROM/TO in label1", "[FmsOrigin]")
{
    const ToLissMcduSnapshot snapshot{"INIT", "CO RTE    CRZ FL", "BIOGRO01        LEBB/LEGE"};
    CHECK_FALSE(ParseToLissInitPageFromTo(snapshot).has_value());
}

TEST_CASE("ParseToLissInitPageFromTo: cont1b must actually contain the two-ICAO pattern", "[FmsOrigin]")
{
    const ToLissMcduSnapshot snapshot{"INIT", "CO RTE    FROM/TO", "no route programmed"};
    CHECK_FALSE(ParseToLissInitPageFromTo(snapshot).has_value());
}

TEST_CASE("UpdateToLissFmsState: a fresh valid snapshot updates the state", "[FmsOrigin]")
{
    ToLissFmsState state;
    const ToLissMcduSnapshot snapshot{"INIT", "CO RTE    FROM/TO", "BIOGRO01        LEBB/LEGE"};
    UpdateToLissFmsState(state, snapshot, 100.0);

    REQUIRE(state.last_confirmed_origin.has_value());
    REQUIRE(state.last_confirmed_destination.has_value());
    CHECK(*state.last_confirmed_origin == "LEBB");
    CHECK(*state.last_confirmed_destination == "LEGE");
    REQUIRE(state.last_confirmed_at_sec.has_value());
    CHECK(*state.last_confirmed_at_sec == 100.0);
}

TEST_CASE("UpdateToLissFmsState: an unparseable snapshot holds the last confirmed value and timestamp", "[FmsOrigin]")
{
    ToLissFmsState state;
    UpdateToLissFmsState(state, {"INIT", "CO RTE    FROM/TO", "BIOGRO01        LEBB/LEGE"}, 100.0);
    REQUIRE(state.last_confirmed_origin.has_value());

    // MCDU has since switched away from the INIT page -- must not clear
    // the last confirmed value or its timestamp.
    UpdateToLissFmsState(state, {"FPLN", "", ""}, 103.0);

    REQUIRE(state.last_confirmed_origin.has_value());
    CHECK(*state.last_confirmed_origin == "LEBB");
    CHECK(*state.last_confirmed_destination == "LEGE");
    REQUIRE(state.last_confirmed_at_sec.has_value());
    CHECK(*state.last_confirmed_at_sec == 100.0);
}

TEST_CASE("IsFresh: never confirmed is never fresh", "[FmsOrigin]")
{
    CHECK_FALSE(IsFresh(std::nullopt, 100.0, kFmsFreshnessThresholdSec));
}

TEST_CASE("IsFresh: within the threshold is fresh", "[FmsOrigin]")
{
    CHECK(IsFresh(100.0, 104.9, kFmsFreshnessThresholdSec));
    CHECK(IsFresh(100.0, 105.0, kFmsFreshnessThresholdSec));
}

TEST_CASE("IsFresh: past the threshold is stale", "[FmsOrigin]")
{
    CHECK_FALSE(IsFresh(100.0, 105.1, kFmsFreshnessThresholdSec));
}

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
