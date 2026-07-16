#include "core/Format.h"

#include <cmath>
#include <cstdio>

namespace trm::core {

std::string FormatAgo(double elapsedSec)
{
    char buf[32];
    if (elapsedSec < 60.0) {
        std::snprintf(buf, sizeof(buf), "%ds ago", static_cast<int>(std::floor(elapsedSec)));
    } else {
        std::snprintf(buf, sizeof(buf), "%dm ago", static_cast<int>(std::floor(elapsedSec / 60.0)));
    }
    return buf;
}

namespace {
constexpr double kPaToInHg = 0.0002953;
constexpr double kPaToHpa = 0.01;
} // namespace

std::string FormatAltimeter(double pressurePa, PressureUnit unit)
{
    char buf[24];
    if (unit == PressureUnit::kInHg) {
        std::snprintf(buf, sizeof(buf), "%.2f inHg", pressurePa * kPaToInHg);
    } else {
        std::snprintf(buf, sizeof(buf), "%.0f hPa", pressurePa * kPaToHpa);
    }
    return buf;
}

} // namespace trm::core
