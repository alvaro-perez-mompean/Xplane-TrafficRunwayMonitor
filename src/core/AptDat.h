#pragma once

#include <istream>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// apt.dat parsing (X-Plane's default-scenery airport/runway database).
// Only reads row 1 (land airport header), row 100 (land runway), and row
// 99 (end of file).
//
// Pure logic, no filesystem access: ParseAptDat takes an already-open
// stream so it's directly unit-testable against a synthetic fixture. Locating
// and opening the real default apt.dat file is sdk/AptDatLoader's job, not
// this module's.

namespace trm::core {

struct RunwayEnd {
    std::string id;
    double lat_deg = 0.0;
    double lon_deg = 0.0;
    double heading_deg = 0.0;
    double width_m = 0.0;
    std::string other_end_id;
    double length_ft = 0.0;
};

struct Airport {
    std::string icao;
    std::string name;
    double elevation_ft = 0.0;
    std::vector<RunwayEnd> runways;
    double ref_lat_deg = 0.0;
    double ref_lon_deg = 0.0;

    // apt.dat has no explicit single airport reference point; ref_lat/lon
    // are the centroid of this airport's runway thresholds, and are only
    // meaningful when there's at least one runway.
    bool HasReferencePoint() const { return !runways.empty(); }
};

using AirportDatabase = std::unordered_map<std::string, Airport>;

AirportDatabase ParseAptDat(std::istream& in);

struct NearbyAirport {
    std::string icao;
    std::string name;
    double distance_nm = 0.0;
};

// Every airport in `db` within radiusNm of (userLat, userLon), nearest
// first. Only airports with a reference point (HasReferencePoint()) are
// considered.
std::vector<NearbyAirport> FindNearestAirports(const AirportDatabase& db, double userLatDeg, double userLonDeg,
                                                double radiusNm);

// Distance from (userLat, userLon) to icao's centroid reference point, or
// nullopt if icao isn't in `db` or has no reference point.
std::optional<double> AirportDistanceNm(const AirportDatabase& db, const std::string& icao, double userLatDeg,
                                         double userLonDeg);

// Looks up a runway end's pavement length (computed once at parse time).
// Returns nullopt if no runway with that id exists on this airport -- a
// safe lookup rather than an assumed one, matching every other apt.dat-
// touching function in this codebase.
std::optional<double> FindRunwayLengthFt(const Airport& airport, const std::string& runwayId);

} // namespace trm::core
