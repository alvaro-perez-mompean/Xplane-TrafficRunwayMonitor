#include "core/SlotValidity.h"

#include <cmath>

namespace trm::core {

bool IsSlotValid(SlotHistory& history, double x, double y, double z, double nowSec,
                  const SlotValidityConfig& config)
{
    if (std::isnan(x) || std::isnan(y) || std::isnan(z)) {
        return false;
    }
    if ((x == 0.0 && y == 0.0 && z == 0.0) || std::abs(x) > config.invalid_coord_magnitude) {
        return false;
    }

    if (!history.has_first_seen) {
        history.first_seen_time_sec = nowSec;
        history.has_first_seen = true;
    }
    if (history.has_last_position
        && (x != history.last_x || y != history.last_y || z != history.last_z)) {
        history.ever_moved = true;
    }
    history.last_x = x;
    history.last_y = y;
    history.last_z = z;
    history.has_last_position = true;

    if (!history.ever_moved && (nowSec - history.first_seen_time_sec) > config.stale_slot_grace_period_sec) {
        return false;
    }

    return true;
}

} // namespace trm::core
