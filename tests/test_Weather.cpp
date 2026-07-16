#include <catch2/catch_test_macros.hpp>

#include "core/Weather.h"

using namespace trm::core;

TEST_CASE("PickLowestAltitudeWindLayer: empty input yields nullopt", "[Weather]")
{
    CHECK_FALSE(PickLowestAltitudeWindLayer({}).has_value());
}

TEST_CASE("PickLowestAltitudeWindLayer: picks the layer with the lowest altitude, "
          "regardless of array order",
          "[Weather]")
{
    const std::vector<WindLayer> layers = {
        WindLayer{3000.0f, 25.0f, 270.0f},
        WindLayer{500.0f, 8.0f, 090.0f}, // lowest
        WindLayer{1500.0f, 15.0f, 180.0f},
    };

    const auto result = PickLowestAltitudeWindLayer(layers);
    REQUIRE(result.has_value());
    CHECK(result->altitude_msl_m == 500.0f);
    CHECK(result->speed_kt == 8.0f);
    CHECK(result->direction_true_deg == 90.0f);
}

TEST_CASE("PickLowestAltitudeWindLayer: a single layer is trivially the lowest", "[Weather]")
{
    const std::vector<WindLayer> layers = {WindLayer{1000.0f, 10.0f, 45.0f}};
    const auto result = PickLowestAltitudeWindLayer(layers);
    REQUIRE(result.has_value());
    CHECK(result->altitude_msl_m == 1000.0f);
}
