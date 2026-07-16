#include <catch2/catch_test_macros.hpp>

#include "core/Format.h"

using namespace trm::core;

TEST_CASE("FormatAgo: under a minute uses whole seconds", "[Format]")
{
    CHECK(FormatAgo(0.0) == "0s ago");
    CHECK(FormatAgo(45.9) == "45s ago");
    CHECK(FormatAgo(59.0) == "59s ago");
}

TEST_CASE("FormatAgo: a minute or more uses whole minutes", "[Format]")
{
    CHECK(FormatAgo(60.0) == "1m ago");
    CHECK(FormatAgo(125.0) == "2m ago");
    CHECK(FormatAgo(3599.0) == "59m ago");
}

TEST_CASE("FormatAltimeter: inHg", "[Format]")
{
    // 101325 Pa == standard sea-level pressure == 29.92 inHg
    CHECK(FormatAltimeter(101325.0, PressureUnit::kInHg) == "29.92 inHg");
}

TEST_CASE("FormatAltimeter: hPa", "[Format]")
{
    // 101325 Pa == standard sea-level pressure == 1013 hPa
    CHECK(FormatAltimeter(101325.0, PressureUnit::kHpa) == "1013 hPa");
}
