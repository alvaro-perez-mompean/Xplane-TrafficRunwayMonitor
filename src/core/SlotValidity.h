#pragma once

// Per-slot validity filtering for TCAS/legacy multiplayer traffic slots.
// Pulled out of sdk/TcasSource into core/ specifically because it's pure
// logic (plain x/y/z/time in, bool out) with zero XPLM dependency, so it
// stays unit-testable even though TcasSource itself (thin XPLM glue) isn't.

namespace trm::core {

struct SlotValidityConfig {
    double stale_slot_grace_period_sec = 30.0;
    double invalid_coord_magnitude = 1000000.0;
};

// Per-slot bookkeeping the caller owns across cycles, one instance per
// tracked slot index.
struct SlotHistory {
    bool ever_moved = false;
    bool has_last_position = false;
    double last_x = 0.0;
    double last_y = 0.0;
    double last_z = 0.0;
    bool has_first_seen = false;
    double first_seen_time_sec = 0.0;
};

// A "live" aircraft worth analyzing: not the (0,0,0) empty-slot sentinel,
// not the large-magnitude empty-slot sentinel modern XPMP2-based traffic
// uses instead, not NaN, and not a default AI plane that's sat frozen since
// first seen (i.e. never actually taken over by real traffic). Updates
// `history` in place.
bool IsSlotValid(SlotHistory& history, double x, double y, double z, double nowSec,
                  const SlotValidityConfig& config = {});

} // namespace trm::core
