#include "core/GeoMath.h"

#include <algorithm>
#include <cmath>

namespace trm::core {

namespace {

// Not relying on M_PI: MSVC only defines it with _USE_MATH_DEFINES set
// before <cmath>, which is fragile to depend on across compilers.
constexpr double kPi = 3.14159265358979323846;

// Floor-division modulo: the result always has the same sign as the
// divisor (non-negative for a positive modulus like 360). C++'s std::fmod
// does not have that guarantee for negative operands, so this wrapper
// restores it -- required for AngleDiffDeg/InitialBearingDeg to behave
// correctly across the 0/360 wraparound.
double NonNegativeMod(double a, double m)
{
    double r = std::fmod(a, m);
    if (r < 0.0) {
        r += m;
    }
    return r;
}

} // namespace

double ToRadians(double degrees)
{
    return degrees * kPi / 180.0;
}

double ToDegrees(double radians)
{
    return radians * 180.0 / kPi;
}

double AngleDiffDeg(double a, double b)
{
    return NonNegativeMod(a - b + 180.0, 360.0) - 180.0;
}

double GreatCircleDistanceNm(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg)
{
    const double phi1 = ToRadians(lat1Deg);
    const double phi2 = ToRadians(lat2Deg);
    const double dPhi = ToRadians(lat2Deg - lat1Deg);
    const double dLambda = ToRadians(lon2Deg - lon1Deg);

    const double sinDPhiHalf = std::sin(dPhi / 2.0);
    const double sinDLambdaHalf = std::sin(dLambda / 2.0);
    const double a = sinDPhiHalf * sinDPhiHalf
                    + std::cos(phi1) * std::cos(phi2) * sinDLambdaHalf * sinDLambdaHalf;
    const double c = 2.0 * std::asin(std::min(1.0, std::sqrt(a)));
    return kEarthRadiusNm * c;
}

double InitialBearingDeg(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg)
{
    const double phi1 = ToRadians(lat1Deg);
    const double phi2 = ToRadians(lat2Deg);
    const double dLambda = ToRadians(lon2Deg - lon1Deg);

    const double y = std::sin(dLambda) * std::cos(phi2);
    const double x = std::cos(phi1) * std::sin(phi2) - std::sin(phi1) * std::cos(phi2) * std::cos(dLambda);
    const double theta = std::atan2(y, x);
    return NonNegativeMod(ToDegrees(theta), 360.0);
}

LocalOffsetFt LocalOffsetFromReference(double latDeg, double lonDeg, double refLatDeg, double refLonDeg)
{
    const double kFtPerRad = kEarthRadiusNm * kNmToFt;
    LocalOffsetFt offset;
    offset.north_ft = ToRadians(latDeg - refLatDeg) * kFtPerRad;
    offset.east_ft = ToRadians(lonDeg - refLonDeg) * kFtPerRad * std::cos(ToRadians(refLatDeg));
    return offset;
}

} // namespace trm::core
