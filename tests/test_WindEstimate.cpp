#include <catch2/catch_test_macros.hpp>

#include "core/WindEstimate.h"

using namespace trm::core;

namespace {

Airport MakeTwoEndedAirport()
{
    Airport airport;
    airport.icao = "KTST";
    airport.runways.push_back(RunwayEnd{"09", 0.0, 0.0, 90.0, 45.0, "27", 5000.0});
    airport.runways.push_back(RunwayEnd{"27", 0.0, 0.0, 270.0, 45.0, "09", 5000.0});
    return airport;
}

} // namespace

TEST_CASE("ResolveEffectiveWind: airport-position reading wins, tags source by station match",
          "[WindEstimate]")
{
    SECTION("station match -> kStation") {
        const WindReading reading{10.0, 95.0, true};
        const auto wind = ResolveEffectiveWind(reading, std::nullopt);
        REQUIRE(wind.has_value());
        CHECK(wind->speed_kt == 10.0);
        CHECK(wind->direction_true_deg == 95.0);
        CHECK(wind->source == WindEstimateSource::kStation);
        CHECK_FALSE(wind->is_calm);
    }
    SECTION("no station match -> kRegional") {
        const WindReading reading{10.0, 95.0, false};
        const auto wind = ResolveEffectiveWind(reading, std::nullopt);
        REQUIRE(wind.has_value());
        CHECK(wind->source == WindEstimateSource::kRegional);
    }
}

TEST_CASE("ResolveEffectiveWind: falls back to aircraft-position reading when airport-position is absent",
          "[WindEstimate]")
{
    const WindReading aircraftReading{8.0, 270.0, false};
    const auto wind = ResolveEffectiveWind(std::nullopt, aircraftReading);
    REQUIRE(wind.has_value());
    CHECK(wind->speed_kt == 8.0);
    CHECK(wind->direction_true_deg == 270.0);
    CHECK(wind->source == WindEstimateSource::kAircraftPosition);
}

TEST_CASE("ResolveEffectiveWind: a dead-calm airport-position reading is still returned (is_calm true), "
          "not overridden by a stronger aircraft-position reading",
          "[WindEstimate]")
{
    const WindReading calmAirportReading{0.0, 90.0, true};
    const WindReading strongAircraftReading{20.0, 270.0, false};

    const auto wind = ResolveEffectiveWind(calmAirportReading, strongAircraftReading);
    REQUIRE(wind.has_value());
    CHECK(wind->source == WindEstimateSource::kStation);
    CHECK(wind->is_calm);
}

TEST_CASE("ResolveEffectiveWind: is_calm reflects the configured threshold", "[WindEstimate]")
{
    WindEstimateConfig config;
    config.min_speed_kt = 1.0;

    SECTION("exactly at threshold -> not calm") {
        const WindReading reading{1.0, 90.0, true};
        const auto wind = ResolveEffectiveWind(reading, std::nullopt, config);
        REQUIRE(wind.has_value());
        CHECK_FALSE(wind->is_calm);
    }
    SECTION("just under threshold -> calm") {
        const WindReading reading{0.99, 90.0, true};
        const auto wind = ResolveEffectiveWind(reading, std::nullopt, config);
        REQUIRE(wind.has_value());
        CHECK(wind->is_calm);
    }
}

TEST_CASE("ResolveEffectiveWind: no readings at all returns nullopt", "[WindEstimate]")
{
    CHECK_FALSE(ResolveEffectiveWind(std::nullopt, std::nullopt).has_value());
}

TEST_CASE("EstimateWindFavoredRunwayEnd: picks the closest-heading runway end", "[WindEstimate]")
{
    const Airport airport = MakeTwoEndedAirport();
    // Wind from 095 -> favors "09" (heading 90, diff 5) over "27" (heading 270, diff 175).
    const WindReading reading{10.0, 95.0, true};

    const auto result = EstimateWindFavoredRunwayEnd(airport, reading, std::nullopt);
    REQUIRE(result.has_value());
    CHECK(result->runway_id == "09");
    CHECK(result->source == WindEstimateSource::kStation);
}

TEST_CASE("EstimateWindFavoredRunwayEnd: has_station_match distinguishes station vs regional", "[WindEstimate]")
{
    const Airport airport = MakeTwoEndedAirport();

    SECTION("station match -> kStation") {
        const WindReading reading{10.0, 90.0, true};
        const auto result = EstimateWindFavoredRunwayEnd(airport, reading, std::nullopt);
        REQUIRE(result.has_value());
        CHECK(result->source == WindEstimateSource::kStation);
    }
    SECTION("no station match -> kRegional") {
        const WindReading reading{10.0, 90.0, false};
        const auto result = EstimateWindFavoredRunwayEnd(airport, reading, std::nullopt);
        REQUIRE(result.has_value());
        CHECK(result->source == WindEstimateSource::kRegional);
    }
}

TEST_CASE("EstimateWindFavoredRunwayEnd: falls back to aircraft-position reading when the "
          "airport-position reading is unavailable",
          "[WindEstimate]")
{
    const Airport airport = MakeTwoEndedAirport();
    const WindReading aircraftReading{8.0, 270.0, false}; // has_station_match unused for this source

    const auto result = EstimateWindFavoredRunwayEnd(airport, std::nullopt, aircraftReading);
    REQUIRE(result.has_value());
    CHECK(result->runway_id == "27");
    CHECK(result->source == WindEstimateSource::kAircraftPosition);
}

TEST_CASE("EstimateWindFavoredRunwayEnd regression: a real but dead-calm airport-position "
          "reading is trusted as final and does not fall through to the aircraft-position reading",
          "[WindEstimate]")
{
    const Airport airport = MakeTwoEndedAirport();
    const WindReading calmAirportReading{0.0, 90.0, true};   // below min_speed_kt
    const WindReading strongAircraftReading{20.0, 270.0, false}; // would otherwise be very usable

    const auto result = EstimateWindFavoredRunwayEnd(airport, calmAirportReading, strongAircraftReading);
    CHECK_FALSE(result.has_value());
}

TEST_CASE("EstimateWindFavoredRunwayEnd: below the minimum speed threshold favors nothing", "[WindEstimate]")
{
    const Airport airport = MakeTwoEndedAirport();
    WindEstimateConfig config;
    config.min_speed_kt = 1.0;

    SECTION("exactly at threshold -> favors something") {
        const WindReading reading{1.0, 90.0, true};
        CHECK(EstimateWindFavoredRunwayEnd(airport, reading, std::nullopt, config).has_value());
    }
    SECTION("just under threshold -> favors nothing") {
        const WindReading reading{0.99, 90.0, true};
        CHECK_FALSE(EstimateWindFavoredRunwayEnd(airport, reading, std::nullopt, config).has_value());
    }
}

TEST_CASE("EstimateWindFavoredRunwayEnd: no readings at all favors nothing", "[WindEstimate]")
{
    const Airport airport = MakeTwoEndedAirport();
    CHECK_FALSE(EstimateWindFavoredRunwayEnd(airport, std::nullopt, std::nullopt).has_value());
}

TEST_CASE("EstimateWindFavoredRunwayEnd: an airport with no runways favors nothing", "[WindEstimate]")
{
    Airport airport;
    airport.icao = "KEMP";
    const WindReading reading{10.0, 90.0, true};
    CHECK_FALSE(EstimateWindFavoredRunwayEnd(airport, reading, std::nullopt).has_value());
}

TEST_CASE("UpgradeToOwnStationIfConfirmed", "[WindEstimate]")
{
    CHECK(UpgradeToOwnStationIfConfirmed(WindEstimateSource::kStation, true) == WindEstimateSource::kOwnStation);
    CHECK(UpgradeToOwnStationIfConfirmed(WindEstimateSource::kStation, false) == WindEstimateSource::kStation);
    CHECK(UpgradeToOwnStationIfConfirmed(WindEstimateSource::kRegional, true) == WindEstimateSource::kRegional);
    CHECK(UpgradeToOwnStationIfConfirmed(WindEstimateSource::kAircraftPosition, true)
          == WindEstimateSource::kAircraftPosition);
}

TEST_CASE("WindEstimateSourceLabel: every tier has a distinct, non-empty label", "[WindEstimate]")
{
    const std::string ownStation = WindEstimateSourceLabel(WindEstimateSource::kOwnStation);
    const std::string station = WindEstimateSourceLabel(WindEstimateSource::kStation);
    const std::string regional = WindEstimateSourceLabel(WindEstimateSource::kRegional);
    const std::string aircraft = WindEstimateSourceLabel(WindEstimateSource::kAircraftPosition);

    CHECK_FALSE(ownStation.empty());
    CHECK_FALSE(station.empty());
    CHECK_FALSE(regional.empty());
    CHECK_FALSE(aircraft.empty());

    CHECK(ownStation != station);
    CHECK(station != regional);
    CHECK(regional != aircraft);
    CHECK(ownStation != aircraft);
}
