#include <catch2/catch_test_macros.hpp>

#include "core/AdvisoryFormat.h"

using namespace trm::core;

namespace {

RunwaySightingSummary MakeActive(const std::string& runwayId, int count = 1, double elapsedSec = 10.0)
{
    RunwaySightingSummary s;
    s.runway_id = runwayId;
    s.count = count;
    s.elapsed_sec = elapsedSec;
    return s;
}

} // namespace

// --- BuildAdvisoryClauses: per-category resolution order ---

TEST_CASE("BuildAdvisoryClauses: active runways win over history and wind estimate", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.icao = "KTST";
    entry.arrivals.active = {MakeActive("31")};
    entry.departures.history = RunwaySightingSummary{"24", 1, 300.0, std::nullopt};

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 2);
    CHECK(clauses[0].category == AdvisoryCategory::kArrival);
    CHECK(clauses[0].tier == AdvisoryTier::kActive);
    CHECK(clauses[0].runway_ids == std::vector<std::string>{"31"});
    CHECK(clauses[1].category == AdvisoryCategory::kDeparture);
    CHECK(clauses[1].tier == AdvisoryTier::kHistory);
    CHECK(clauses[1].runway_ids == std::vector<std::string>{"24"});
    CHECK(clauses[1].elapsed_sec == 300.0);
}

TEST_CASE("BuildAdvisoryClauses: multiple simultaneous active runways are all included, joined", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("04L"), MakeActive("04R")};
    entry.departures.active = {MakeActive("31L"), MakeActive("31R")};

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 2);
    CHECK(clauses[0].runway_ids == std::vector<std::string>{"04L", "04R"});
    CHECK(clauses[1].runway_ids == std::vector<std::string>{"31L", "31R"});
}

TEST_CASE("BuildAdvisoryClauses: falls back to wind estimate only when active and history are both empty",
          "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    // departures: no active, no history -> needs wind estimate
    entry.wind_estimate = WindEstimateResult{"04", WindEstimateSource::kRegional};

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 2);
    CHECK(clauses[1].tier == AdvisoryTier::kWindEstimate);
    CHECK(clauses[1].runway_ids == std::vector<std::string>{"04"});
    CHECK(clauses[1].wind_source == WindEstimateSource::kRegional);
}

TEST_CASE("BuildAdvisoryClauses: no active/history/wind-estimate resolves to kNone", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    // departures: nothing at all, and no wind_estimate available (e.g. dead calm)

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 2);
    CHECK(clauses[1].tier == AdvisoryTier::kNone);
    CHECK(clauses[1].runway_ids.empty());
}

// --- BuildAdvisoryClauses: collapsing rule ---

TEST_CASE("BuildAdvisoryClauses: collapses to kBoth on exact same tier and runway set", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    entry.departures.active = {MakeActive("31")};

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 1);
    CHECK(clauses[0].category == AdvisoryCategory::kBoth);
    CHECK(clauses[0].tier == AdvisoryTier::kActive);
    CHECK(clauses[0].runway_ids == std::vector<std::string>{"31"});
}

TEST_CASE("BuildAdvisoryClauses: collapsing is order-independent on the runway set", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("04L"), MakeActive("04R")};
    entry.departures.active = {MakeActive("04R"), MakeActive("04L")};

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 1);
    CHECK(clauses[0].category == AdvisoryCategory::kBoth);
}

TEST_CASE("BuildAdvisoryClauses: partial runway-set overlap does NOT collapse", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    entry.departures.active = {MakeActive("31"), MakeActive("24")};

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 2);
    CHECK(clauses[0].category == AdvisoryCategory::kArrival);
    CHECK(clauses[1].category == AdvisoryCategory::kDeparture);
}

TEST_CASE("BuildAdvisoryClauses: same runway but mismatched tier does NOT collapse", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("18L")};
    entry.departures.history = RunwaySightingSummary{"18L", 1, 840.0, std::nullopt};

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 2);
    CHECK(clauses[0].tier == AdvisoryTier::kActive);
    CHECK(clauses[1].tier == AdvisoryTier::kHistory);
}

TEST_CASE("BuildAdvisoryClauses: both categories empty collapses to a single kNone/kBoth clause",
          "[AdvisoryFormat]")
{
    AirportEntry entry; // nothing at all, no wind_estimate

    const auto clauses = BuildAdvisoryClauses(entry);
    REQUIRE(clauses.size() == 1);
    CHECK(clauses[0].category == AdvisoryCategory::kBoth);
    CHECK(clauses[0].tier == AdvisoryTier::kNone);
}

// --- FormatAdvisoryPlainText ---

TEST_CASE("FormatAdvisoryPlainText: collapsed active clause plus wind and QNH", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    entry.departures.active = {MakeActive("31")};
    entry.current_wind = WindInfo{8.0, 310.0, WindEstimateSource::kStation, false};

    const auto clauses = BuildAdvisoryClauses(entry);
    const std::string text = FormatAdvisoryPlainText(clauses, entry.current_wind, 101325.0, PressureUnit::kHpa);
    CHECK(text == "Currently landing and departing runway 31, wind 310 at 8, QNH 1013.");
}

TEST_CASE("FormatAdvisoryPlainText: altimeter phraseology follows inHg vs hPa setting", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    entry.departures.active = {MakeActive("31")};

    const auto clauses = BuildAdvisoryClauses(entry);
    const std::string inHgText = FormatAdvisoryPlainText(clauses, std::nullopt, 101325.0, PressureUnit::kInHg);
    CHECK(inHgText == "Currently landing and departing runway 31, altimeter 29.92.");
}

TEST_CASE("FormatAdvisoryPlainText: separate clauses for different runways", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("04L"), MakeActive("04R")};
    entry.departures.active = {MakeActive("31L"), MakeActive("31R")};

    const auto clauses = BuildAdvisoryClauses(entry);
    const std::string text = FormatAdvisoryPlainText(clauses, std::nullopt, std::nullopt, PressureUnit::kHpa);
    CHECK(text == "Currently landing runways 04L and 04R, currently departing runways 31L and 31R.");
}

TEST_CASE("FormatAdvisoryPlainText: history clause includes elapsed time", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    entry.departures.history = RunwaySightingSummary{"18L", 1, 840.0, std::nullopt};

    const auto clauses = BuildAdvisoryClauses(entry);
    const std::string text = FormatAdvisoryPlainText(clauses, std::nullopt, std::nullopt, PressureUnit::kHpa);
    CHECK(text == "Currently landing runway 31, recently departed runway 18L (14m ago).");
}

TEST_CASE("FormatAdvisoryPlainText: wind-estimate clause caveats uncertain sources only", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};

    entry.wind_estimate = WindEstimateResult{"04", WindEstimateSource::kRegional};
    auto clauses = BuildAdvisoryClauses(entry);
    CHECK(FormatAdvisoryPlainText(clauses, std::nullopt, std::nullopt, PressureUnit::kHpa) ==
          "Currently landing runway 31, wind favors runway 04 for departures (regional estimate).");

    entry.wind_estimate = WindEstimateResult{"04", WindEstimateSource::kAircraftPosition};
    clauses = BuildAdvisoryClauses(entry);
    CHECK(FormatAdvisoryPlainText(clauses, std::nullopt, std::nullopt, PressureUnit::kHpa) ==
          "Currently landing runway 31, wind favors runway 04 for departures (aircraft-based estimate).");
}

TEST_CASE("FormatAdvisoryPlainText: wind-estimate clause says nothing extra for trustworthy sources",
          "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};

    entry.wind_estimate = WindEstimateResult{"04", WindEstimateSource::kOwnStation};
    auto clauses = BuildAdvisoryClauses(entry);
    CHECK(FormatAdvisoryPlainText(clauses, std::nullopt, std::nullopt, PressureUnit::kHpa) ==
          "Currently landing runway 31, wind favors runway 04 for departures.");

    entry.wind_estimate = WindEstimateResult{"04", WindEstimateSource::kStation};
    clauses = BuildAdvisoryClauses(entry);
    CHECK(FormatAdvisoryPlainText(clauses, std::nullopt, std::nullopt, PressureUnit::kHpa) ==
          "Currently landing runway 31, wind favors runway 04 for departures.");
}

TEST_CASE("FormatAdvisoryPlainText: no traffic at all collapses to one unqualified clause", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.current_wind = WindInfo{0.0, 0.0, WindEstimateSource::kRegional, /*is_calm=*/true};

    const auto clauses = BuildAdvisoryClauses(entry);
    const std::string text = FormatAdvisoryPlainText(clauses, entry.current_wind, std::nullopt, PressureUnit::kHpa);
    CHECK(text == "No traffic information, wind calm.");
}

TEST_CASE("FormatAdvisoryPlainText: wind and altimeter clauses omitted when data is unavailable",
          "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("31")};
    entry.departures.active = {MakeActive("31")};

    const auto clauses = BuildAdvisoryClauses(entry);
    const std::string text = FormatAdvisoryPlainText(clauses, std::nullopt, std::nullopt, PressureUnit::kHpa);
    CHECK(text == "Currently landing and departing runway 31.");
}

// --- FormatAdvisorySpoken ---

TEST_CASE("FormatAdvisorySpoken: runway IDs are phonetic, wind/altimeter numbers are not", "[AdvisoryFormat]")
{
    AirportEntry entry;
    entry.arrivals.active = {MakeActive("24L")};
    entry.departures.active = {MakeActive("24L")};
    entry.current_wind = WindInfo{8.0, 310.0, WindEstimateSource::kStation, false};

    const auto clauses = BuildAdvisoryClauses(entry);
    const std::string text = FormatAdvisorySpoken(clauses, entry.current_wind, 101325.0, PressureUnit::kHpa);
    CHECK(text == "Currently landing and departing runway two four left, wind 310 at 8, QNH 1013.");
}

// --- SpokenRunwayId ---

TEST_CASE("SpokenRunwayId: digits spelled out individually, letter suffix as a trailing word",
          "[AdvisoryFormat]")
{
    CHECK(SpokenRunwayId("24L") == "two four left");
    CHECK(SpokenRunwayId("06") == "zero six");
    CHECK(SpokenRunwayId("31") == "three one");
    CHECK(SpokenRunwayId("9") == "nine");
    CHECK(SpokenRunwayId("18C") == "one eight center");
    CHECK(SpokenRunwayId("09R") == "zero nine right");
}
