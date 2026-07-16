#pragma once

// Shared great-circle math used by AptDat (runway heading/length derivation)
// and RunwayMatcher (heading/lateral/along-track matching).

namespace trm::core {

constexpr double kEarthRadiusNm = 3440.065;
constexpr double kNmToFt = 6076.12;

double ToRadians(double degrees);
double ToDegrees(double radians);

// Signed smallest angle from b to a, result in [-180, 180).
double AngleDiffDeg(double a, double b);

// Great-circle distance between two lat/lon points, in nautical miles.
double GreatCircleDistanceNm(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// Initial true-north bearing (degrees, [0, 360)) from point 1 to point 2.
double InitialBearingDeg(double lat1Deg, double lon1Deg, double lat2Deg, double lon2Deg);

// East/north offset in feet, on a local tangent plane centered at a
// reference point. Used to place things (e.g. runway thresholds) on a
// flat, to-scale diagram rather than a pure angle-only plot.
struct LocalOffsetFt {
    double east_ft = 0.0;
    double north_ft = 0.0;
};

// Equirectangular approximation (flat-earth, longitude scaled by
// cos(refLatDeg)) -- accurate to a small fraction of a foot of error at
// airport scale (a few nautical miles across at most), not meant for
// long-range navigation math.
LocalOffsetFt LocalOffsetFromReference(double latDeg, double lonDeg, double refLatDeg, double refLonDeg);

} // namespace trm::core
