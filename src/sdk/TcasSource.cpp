#include "sdk/TcasSource.h"

#include "XPLMDataAccess.h"
#include "XPLMGraphics.h"

#include <cmath>
#include <string>

namespace trm::sdk {

namespace {
constexpr int kMpSlotFirst = 1;
constexpr int kMpSlotLast = 19;
constexpr int kMaxTcasSlots = 63;
// TCAS Override arrays are hard-capped at kMaxTcasSlots (63) AI aircraft,
// but index 0 of the array is the user's own aircraft (confirmed in-sim,
// see PLAN-extend-to-63-aircraft.md), so a 63rd *AI* slot requires fetching
// 64 elements starting at index 0.
constexpr int kTcasFetchLen = kMaxTcasSlots + 1;
constexpr double kMpsToKt = 1.9438444924406;
} // namespace

struct TcasSource::TcasDataRefs {
    XPLMDataRef x = nullptr;
    XPLMDataRef y = nullptr;
    XPLMDataRef z = nullptr;
    XPLMDataRef vx = nullptr;
    XPLMDataRef vy = nullptr;
    XPLMDataRef vz = nullptr;
    XPLMDataRef psi = nullptr;

    bool AllResolved() const { return x && y && z && vx && vy && vz && psi; }
};

// mp_datarefs[i]: no plane<i>_lat/_lon/_elevation dataref exists for
// multiplayer slots -- only the user's own aircraft exposes geodetic
// position directly, so every slot's position is derived from local
// (x, y, z) via XPLMLocalToWorld, same as the TCAS path.
struct TcasSource::LegacySlotDataRefs {
    XPLMDataRef x = nullptr;
    XPLMDataRef y = nullptr;
    XPLMDataRef z = nullptr;
    XPLMDataRef v_x = nullptr;
    XPLMDataRef v_y = nullptr;
    XPLMDataRef v_z = nullptr;
    XPLMDataRef psi = nullptr;
};

TcasSource::TcasSource()
    : tcas_refs_(std::make_unique<TcasDataRefs>())
{
    tcas_refs_->x = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/x");
    tcas_refs_->y = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/y");
    tcas_refs_->z = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/z");
    tcas_refs_->vx = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/vx");
    tcas_refs_->vy = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/vy");
    tcas_refs_->vz = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/vz");
    tcas_refs_->psi = XPLMFindDataRef("sim/cockpit2/tcas/targets/position/psi");

    has_extended_traffic_ = tcas_refs_->AllResolved();
    slot_count_ = has_extended_traffic_ ? kMaxTcasSlots : kMpSlotLast;

    if (!has_extended_traffic_) {
        legacy_refs_.resize(kMpSlotLast + 1); // index 0 unused
        for (int i = kMpSlotFirst; i <= kMpSlotLast; ++i) {
            const std::string prefix = "sim/multiplayer/position/plane" + std::to_string(i) + "_";
            LegacySlotDataRefs& refs = legacy_refs_[i];
            refs.x = XPLMFindDataRef((prefix + "x").c_str());
            refs.y = XPLMFindDataRef((prefix + "y").c_str());
            refs.z = XPLMFindDataRef((prefix + "z").c_str());
            refs.v_x = XPLMFindDataRef((prefix + "v_x").c_str());
            refs.v_y = XPLMFindDataRef((prefix + "v_y").c_str());
            refs.v_z = XPLMFindDataRef((prefix + "v_z").c_str());
            refs.psi = XPLMFindDataRef((prefix + "psi").c_str());
        }
    }

    slot_history_.resize(static_cast<size_t>(slot_count_) + 1); // index 0 unused
}

TcasSource::~TcasSource() = default;

std::vector<SlotReading> TcasSource::CollectTraffic(double nowSec)
{
    std::vector<SlotReading> readings(static_cast<size_t>(slot_count_) + 1); // index 0 unused

    std::vector<float> tcasX, tcasY, tcasZ, tcasVx, tcasVy, tcasVz, tcasPsi;
    if (has_extended_traffic_) {
        tcasX.resize(kTcasFetchLen);
        tcasY.resize(kTcasFetchLen);
        tcasZ.resize(kTcasFetchLen);
        tcasVx.resize(kTcasFetchLen);
        tcasVy.resize(kTcasFetchLen);
        tcasVz.resize(kTcasFetchLen);
        tcasPsi.resize(kTcasFetchLen);
        XPLMGetDatavf(tcas_refs_->x, tcasX.data(), 0, kTcasFetchLen);
        XPLMGetDatavf(tcas_refs_->y, tcasY.data(), 0, kTcasFetchLen);
        XPLMGetDatavf(tcas_refs_->z, tcasZ.data(), 0, kTcasFetchLen);
        XPLMGetDatavf(tcas_refs_->vx, tcasVx.data(), 0, kTcasFetchLen);
        XPLMGetDatavf(tcas_refs_->vy, tcasVy.data(), 0, kTcasFetchLen);
        XPLMGetDatavf(tcas_refs_->vz, tcasVz.data(), 0, kTcasFetchLen);
        XPLMGetDatavf(tcas_refs_->psi, tcasPsi.data(), 0, kTcasFetchLen);
    }

    for (int i = kMpSlotFirst; i <= slot_count_; ++i) {
        double x, y, z, vx, vy, vz, headingTrue;

        if (has_extended_traffic_) {
            x = tcasX[i];
            y = tcasY[i];
            z = tcasZ[i];
            vx = tcasVx[i];
            vy = tcasVy[i];
            vz = tcasVz[i];
            headingTrue = tcasPsi[i];
        } else {
            const LegacySlotDataRefs& refs = legacy_refs_[i];
            x = XPLMGetDataf(refs.x);
            y = XPLMGetDataf(refs.y);
            z = XPLMGetDataf(refs.z);
            vx = XPLMGetDataf(refs.v_x);
            vy = XPLMGetDataf(refs.v_y);
            vz = XPLMGetDataf(refs.v_z);
            headingTrue = XPLMGetDataf(refs.psi);
        }

        SlotReading& out = readings[i];
        out.slot_index = i;

        if (!core::IsSlotValid(slot_history_[i], x, y, z, nowSec)) {
            out.valid = false;
            continue;
        }

        double latDeg, lonDeg, mslM;
        XPLMLocalToWorld(x, y, z, &latDeg, &lonDeg, &mslM);

        out.valid = true;
        out.lat_deg = latDeg;
        out.lon_deg = lonDeg;
        out.msl_m = mslM;
        out.heading_true_deg = headingTrue;
        out.gs_kt = std::sqrt(vx * vx + vz * vz) * kMpsToKt;
        out.vs_mps = vy;
    }

    return readings;
}

} // namespace trm::sdk
