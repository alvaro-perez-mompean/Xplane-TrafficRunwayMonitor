#include "sdk/LtapiSource.h"

namespace trm::sdk {

LtapiSource::LtapiSource(int maxSlots) : slot_assigner_(maxSlots), max_slots_(maxSlots) {}

bool LtapiSource::IsAvailable() const
{
    return LTAPIConnect::doesLTDisplayAc();
}

std::vector<SlotReading> LtapiSource::CollectTraffic()
{
    std::vector<SlotReading> readings(static_cast<size_t>(max_slots_) + 1); // index 0 unused
    for (int i = 1; i <= max_slots_; ++i) {
        readings[static_cast<size_t>(i)].slot_index = i;
    }

    const MapLTAPIAircraft& acMap = connect_.UpdateAcList();

    std::vector<std::string> keys;
    keys.reserve(acMap.size());
    for (const auto& entry : acMap) {
        keys.push_back(entry.first);
    }

    const auto keyToSlot = slot_assigner_.AssignSlots(keys);

    for (const auto& entry : acMap) {
        const auto it = keyToSlot.find(entry.first);
        if (it == keyToSlot.end()) {
            continue; // soft-ceiling overflow -- dropped this cycle, see core::SlotAssigner
        }
        const int slot = it->second;
        const LTAPIAircraft& ac = *entry.second;

        SlotReading& out = readings[static_cast<size_t>(slot)];
        out.valid = true;
        out.lat_deg = ac.getLat();
        out.lon_deg = ac.getLon();
        out.msl_m = ac.getAltFt() * 0.3048;
        out.heading_true_deg = ac.getHeading();
        out.gs_kt = ac.getSpeedKn();
        out.vs_mps = ac.getVSIft() * 0.3048 / 60.0;
        out.callsign = ac.getCallSign();
        // acClass is DOC 8643's type-of-aircraft + engine-count + engine-type
        // code, e.g. "L2J" (landplane, 2 jets) or "H1T" (helicopter, 1
        // turboshaft) -- the first character is the type-of-aircraft code,
        // 'H' for helicopter.
        const std::string acClass = ac.getAcClass();
        out.is_helicopter = !acClass.empty() && acClass.front() == 'H';
    }

    return readings;
}

} // namespace trm::sdk
