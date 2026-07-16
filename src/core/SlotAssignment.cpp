#include "core/SlotAssignment.h"

#include <unordered_set>

namespace trm::core {

SlotAssigner::SlotAssigner(int maxSlots) : max_slots_(maxSlots) {}

std::unordered_map<std::string, int> SlotAssigner::AssignSlots(const std::vector<std::string>& keysThisCycle)
{
    for (int slot : pending_free_slots_) {
        free_slots_.push_back(slot);
    }
    pending_free_slots_.clear();

    std::unordered_set<std::string> seen;
    std::unordered_map<std::string, int> result;

    for (const std::string& key : keysThisCycle) {
        seen.insert(key);

        auto existing = key_to_slot_.find(key);
        int slot = 0;
        bool haveSlot = false;
        if (existing != key_to_slot_.end()) {
            slot = existing->second;
            haveSlot = true;
        } else if (!free_slots_.empty()) {
            slot = free_slots_.back();
            free_slots_.pop_back();
            haveSlot = true;
        } else if (next_new_slot_ < max_slots_) {
            ++next_new_slot_;
            slot = next_new_slot_;
            haveSlot = true;
        }

        if (haveSlot) {
            key_to_slot_[key] = slot;
            slot_to_key_[slot] = key;
            result[key] = slot;
        } else {
            overflowed_ = true;
        }
    }

    // Any slot still mapped to a key that didn't appear this cycle is now
    // stale -- unmap it, and queue it for reuse starting next cycle (not
    // this one; see this class's own header comment for why).
    for (auto it = slot_to_key_.begin(); it != slot_to_key_.end();) {
        if (seen.find(it->second) == seen.end()) {
            key_to_slot_.erase(it->second);
            pending_free_slots_.push_back(it->first);
            it = slot_to_key_.erase(it);
        } else {
            ++it;
        }
    }

    return result;
}

} // namespace trm::core
