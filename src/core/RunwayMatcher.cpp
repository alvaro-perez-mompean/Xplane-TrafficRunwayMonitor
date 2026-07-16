#include "core/RunwayMatcher.h"
#include "core/GeoMath.h"

#include <cmath>
#include <limits>

namespace trm::core {

const RunwayEnd* MatchRunwayEnd(const Airport& airport, double acLatDeg, double acLonDeg,
                                 double acHeadingTrueDeg, const RunwayMatchConfig& config)
{
    const RunwayEnd* best = nullptr;
    double bestAlongTrackNm = std::numeric_limits<double>::infinity();

    for (const RunwayEnd& rwyEnd : airport.runways) {
        if (std::abs(AngleDiffDeg(acHeadingTrueDeg, rwyEnd.heading_deg)) > config.heading_tolerance_deg) {
            continue;
        }

        // Straight-line distance from the threshold, used as-is (not the
        // along-track decomposition below) for this pre-filter.
        const double distNm = GreatCircleDistanceNm(rwyEnd.lat_deg, rwyEnd.lon_deg, acLatDeg, acLonDeg);
        if (distNm > config.max_along_track_nm) {
            continue;
        }

        const double bearingToAc = InitialBearingDeg(rwyEnd.lat_deg, rwyEnd.lon_deg, acLatDeg, acLonDeg);
        const double trackAngleRad = ToRadians(AngleDiffDeg(bearingToAc, rwyEnd.heading_deg));
        const double alongTrackNm = distNm * std::cos(trackAngleRad);
        const double crossTrackNm = distNm * std::sin(trackAngleRad);

        if (std::abs(crossTrackNm) <= config.lateral_tolerance_nm
            && std::abs(alongTrackNm) < bestAlongTrackNm) {
            best = &rwyEnd;
            bestAlongTrackNm = std::abs(alongTrackNm);
        }
    }

    return best;
}

} // namespace trm::core
