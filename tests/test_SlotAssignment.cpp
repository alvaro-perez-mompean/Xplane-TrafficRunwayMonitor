#include <catch2/catch_test_macros.hpp>

#include "core/SlotAssignment.h"

using namespace trm::core;

TEST_CASE("SlotAssigner: new keys get sequential slots starting at 1", "[SlotAssignment]")
{
    SlotAssigner assigner;
    const auto result = assigner.AssignSlots({"AAA", "BBB", "CCC"});

    CHECK(result.at("AAA") == 1);
    CHECK(result.at("BBB") == 2);
    CHECK(result.at("CCC") == 3);
}

TEST_CASE("SlotAssigner: a key keeps its slot across cycles as long as it keeps appearing", "[SlotAssignment]")
{
    SlotAssigner assigner;
    const auto first = assigner.AssignSlots({"AAA", "BBB"});
    const auto second = assigner.AssignSlots({"BBB", "AAA"}); // order swapped

    CHECK(second.at("AAA") == first.at("AAA"));
    CHECK(second.at("BBB") == first.at("BBB"));
}

TEST_CASE("SlotAssigner: a departed key's slot is not reusable the same cycle it disappears", "[SlotAssignment]")
{
    SlotAssigner assigner;
    const auto first = assigner.AssignSlots({"AAA"});
    const int aaaSlot = first.at("AAA");

    // AAA disappears, BBB shows up in the same cycle: BBB must get a *new*
    // slot, not AAA's just-vacated one -- the whole point of the pending-
    // free-slots gap is to give a full cycle for "this slot went invalid"
    // cleanup to run before the slot number could be silently reused.
    const auto second = assigner.AssignSlots({"BBB"});
    CHECK(second.at("BBB") != aaaSlot);
}

TEST_CASE("SlotAssigner: a freed slot becomes reusable starting the cycle after it's freed", "[SlotAssignment]")
{
    SlotAssigner assigner;
    const auto first = assigner.AssignSlots({"AAA"});
    const int aaaSlot = first.at("AAA");

    assigner.AssignSlots({}); // AAA absent: its slot is queued for reuse next cycle
    const auto third = assigner.AssignSlots({"BBB"}); // now eligible for reuse

    CHECK(third.at("BBB") == aaaSlot);
}

TEST_CASE("SlotAssigner: a key that reappears after its old slot was reused by someone else gets a different slot",
          "[SlotAssignment]")
{
    // A freed slot goes back into a shared pool, not reserved for whoever
    // held it before -- if another key claims it first, the original key
    // (once it's treated as a brand-new request) can't reclaim its old
    // identity; it just gets whatever's next available.
    SlotAssigner assigner;
    const auto first = assigner.AssignSlots({"AAA"});
    const int aaaSlot = first.at("AAA");

    assigner.AssignSlots({}); // AAA absent for a full cycle -- its old slot is now free
    const auto third = assigner.AssignSlots({"BBB"}); // BBB claims the freed slot
    CHECK(third.at("BBB") == aaaSlot);

    const auto fourth = assigner.AssignSlots({"BBB", "AAA"}); // AAA reappears; its old slot is taken
    CHECK(fourth.at("AAA") != aaaSlot);
}

TEST_CASE("SlotAssigner: exceeding the soft ceiling drops the overflowing key without disturbing existing slots",
          "[SlotAssignment]")
{
    SlotAssigner assigner(/*maxSlots=*/2);
    const auto first = assigner.AssignSlots({"AAA", "BBB"});
    CHECK_FALSE(assigner.HasOverflowed());

    const auto second = assigner.AssignSlots({"AAA", "BBB", "CCC"});
    CHECK(second.at("AAA") == first.at("AAA"));
    CHECK(second.at("BBB") == first.at("BBB"));
    CHECK(second.find("CCC") == second.end());
    CHECK(assigner.HasOverflowed());
}

TEST_CASE("SlotAssigner: an empty cycle assigns nothing and frees every previously held slot", "[SlotAssignment]")
{
    SlotAssigner assigner;
    assigner.AssignSlots({"AAA", "BBB"});
    const auto empty = assigner.AssignSlots({});
    CHECK(empty.empty());

    const auto next = assigner.AssignSlots({"CCC"});
    // CCC should reuse one of AAA/BBB's now-freed slots (1 or 2), not a new slot 3.
    CHECK(next.at("CCC") <= 2);
}
