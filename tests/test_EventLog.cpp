#include <catch2/catch_test_macros.hpp>

#include "core/EventLog.h"

using namespace trm::core;

TEST_CASE("EventLog: Summaries returns most-recent-first with elapsed_sec relative to nowSec", "[EventLog]")
{
    EventLog log;
    log.Record(RunwayEvent{"KTST", SightingCategory::kDeparture, "09", 10.0});
    log.Record(RunwayEvent{"KTST", SightingCategory::kArrival, "27", 40.0});

    const auto summaries = log.Summaries(50.0);
    REQUIRE(summaries.size() == 2);
    CHECK(summaries[0].runway_id == "27"); // recorded second, so most recent
    CHECK(summaries[0].elapsed_sec == 10.0);
    CHECK(summaries[1].runway_id == "09");
    CHECK(summaries[1].elapsed_sec == 40.0);
}

TEST_CASE("EventLog: PruneOlderThan drops events past maxAgeSec, keeps the rest", "[EventLog]")
{
    EventLog log;
    log.Record(RunwayEvent{"KTST", SightingCategory::kDeparture, "09", 0.0});
    log.Record(RunwayEvent{"KTST", SightingCategory::kArrival, "27", 100.0});

    log.PruneOlderThan(/*nowSec=*/200.0, /*maxAgeSec=*/150.0);

    const auto summaries = log.Summaries(200.0);
    REQUIRE(summaries.size() == 1);
    CHECK(summaries[0].runway_id == "27");
}

TEST_CASE("EventLog: empty log returns no summaries", "[EventLog]")
{
    EventLog log;
    CHECK(log.Summaries(123.0).empty());
}

TEST_CASE("EventLog: Summaries carries callsign through, empty when the event didn't have one", "[EventLog]")
{
    EventLog log;
    log.Record(RunwayEvent{"KTST", SightingCategory::kDeparture, "09", 10.0, "DLH56C"});
    log.Record(RunwayEvent{"KTST", SightingCategory::kArrival, "27", 20.0});

    const auto summaries = log.Summaries(30.0);
    REQUIRE(summaries.size() == 2);
    CHECK(summaries[0].callsign.empty());
    CHECK(summaries[1].callsign == "DLH56C");
}
