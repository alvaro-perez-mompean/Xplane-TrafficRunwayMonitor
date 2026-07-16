#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "core/SlotValidity.h"

using namespace trm::core;

TEST_CASE("IsSlotValid rejects the zero sentinel", "[SlotValidity]")
{
    SlotHistory history;
    CHECK_FALSE(IsSlotValid(history, 0.0, 0.0, 0.0, 100.0));
}

TEST_CASE("IsSlotValid rejects the large-magnitude sentinel", "[SlotValidity]")
{
    SlotHistory history;
    CHECK_FALSE(IsSlotValid(history, 2000000.0, 5.0, 5.0, 100.0));
    // Only x's magnitude is checked.
    SlotHistory history2;
    CHECK(IsSlotValid(history2, 500.0, 5.0, 5.0, 100.0));
}

TEST_CASE("IsSlotValid rejects NaN in any coordinate", "[SlotValidity]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    SlotHistory h1;
    CHECK_FALSE(IsSlotValid(h1, nan, 1.0, 1.0, 100.0));
    SlotHistory h2;
    CHECK_FALSE(IsSlotValid(h2, 1.0, nan, 1.0, 100.0));
    SlotHistory h3;
    CHECK_FALSE(IsSlotValid(h3, 1.0, 1.0, nan, 100.0));
}

TEST_CASE("IsSlotValid accepts a real position immediately, within the grace period", "[SlotValidity]")
{
    SlotHistory history;
    CHECK(IsSlotValid(history, 100.0, 200.0, 300.0, 0.0));
}

TEST_CASE("IsSlotValid: a slot frozen past the grace period without moving goes invalid", "[SlotValidity]")
{
    SlotValidityConfig config;
    config.stale_slot_grace_period_sec = 30.0;
    SlotHistory history;

    CHECK(IsSlotValid(history, 100.0, 200.0, 300.0, 0.0, config));   // first seen
    CHECK(IsSlotValid(history, 100.0, 200.0, 300.0, 30.0, config));  // exactly at grace period: still valid (strict > comparison)
    CHECK_FALSE(IsSlotValid(history, 100.0, 200.0, 300.0, 30.1, config)); // now stale
}

TEST_CASE("IsSlotValid: once a slot has moved, it stays valid even if it later stops moving", "[SlotValidity]")
{
    SlotValidityConfig config;
    config.stale_slot_grace_period_sec = 30.0;
    SlotHistory history;

    CHECK(IsSlotValid(history, 100.0, 200.0, 300.0, 0.0, config));
    CHECK(IsSlotValid(history, 101.0, 200.0, 300.0, 10.0, config)); // moved -> ever_moved = true
    // Frozen again afterwards, well past the grace period -- still valid,
    // because ever_moved latches permanently once true.
    CHECK(IsSlotValid(history, 101.0, 200.0, 300.0, 1000.0, config));
}
