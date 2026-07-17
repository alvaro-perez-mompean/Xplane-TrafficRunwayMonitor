#include "core/AptDat.h"
#include "core/GeoMath.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <sstream>

namespace trm::core {

namespace {

std::vector<std::string> SplitWhitespace(const std::string& line)
{
    std::vector<std::string> tokens;
    std::istringstream iss(line);
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

// 1-indexed token access, matching apt.dat's own field-numbering convention.
std::optional<std::string> Token(const std::vector<std::string>& tokens, size_t oneIndexedPos)
{
    if (oneIndexedPos == 0 || oneIndexedPos > tokens.size()) {
        return std::nullopt;
    }
    return tokens[oneIndexedPos - 1];
}

std::optional<double> TokenAsDouble(const std::vector<std::string>& tokens, size_t oneIndexedPos)
{
    const auto tok = Token(tokens, oneIndexedPos);
    if (!tok) {
        return std::nullopt;
    }
    char* end = nullptr;
    const double value = std::strtod(tok->c_str(), &end);
    if (end == tok->c_str()) {
        return std::nullopt;
    }
    return value;
}

void ComputeReferencePoints(AirportDatabase& db)
{
    for (auto& [icao, airport] : db) {
        if (airport.runways.empty()) {
            continue;
        }
        double sumLat = 0.0;
        double sumLon = 0.0;
        for (const auto& rwyEnd : airport.runways) {
            sumLat += rwyEnd.lat_deg;
            sumLon += rwyEnd.lon_deg;
        }
        const double count = static_cast<double>(airport.runways.size());
        airport.ref_lat_deg = sumLat / count;
        airport.ref_lon_deg = sumLon / count;
    }
}

} // namespace

AirportDatabase ParseAptDat(std::istream& in)
{
    AirportDatabase db;
    std::string currentIcao; // empty = no active airport context

    std::string line;
    while (std::getline(in, line)) {
        const std::vector<std::string> tokens = SplitWhitespace(line);
        if (tokens.empty()) {
            continue;
        }
        const std::string& rowCode = tokens[0];

        if (rowCode == "1") {
            // Row 1 (land airport header): ICAO at token 5, elevation_ft at
            // token 2, airport name at token 6 onward (space-separated, may
            // itself contain spaces -- e.g. "Chicago O'Hare Intl").
            const auto icao = Token(tokens, 5);
            if (icao) {
                currentIcao = *icao;
                auto [it, inserted] = db.try_emplace(currentIcao);
                if (inserted) {
                    it->second.icao = currentIcao;
                    it->second.elevation_ft = TokenAsDouble(tokens, 2).value_or(0.0);
                    if (tokens.size() > 5) {
                        std::string name = tokens[5];
                        for (std::size_t i = 6; i < tokens.size(); ++i) {
                            name += ' ';
                            name += tokens[i];
                        }
                        it->second.name = std::move(name);
                    }
                }
            } else {
                currentIcao.clear();
            }
        } else if (rowCode == "100" && !currentIcao.empty()) {
            // Row 100 (land runway): width_m at token 2; end1 id/lat/lon at
            // tokens 9/10/11; end2 id/lat/lon at tokens 18/19/20. Heading and
            // length have no explicit fields -- derive both from the
            // bearing/distance between the two thresholds.
            const auto width = TokenAsDouble(tokens, 2);
            const auto id1 = Token(tokens, 9);
            const auto lat1 = TokenAsDouble(tokens, 10);
            const auto lon1 = TokenAsDouble(tokens, 11);
            const auto id2 = Token(tokens, 18);
            const auto lat2 = TokenAsDouble(tokens, 19);
            const auto lon2 = TokenAsDouble(tokens, 20);

            if (id1 && lat1 && lon1 && id2 && lat2 && lon2) {
                const double heading1 = InitialBearingDeg(*lat1, *lon1, *lat2, *lon2);
                const double heading2 = std::fmod(heading1 + 180.0, 360.0);
                const double lengthFt = GreatCircleDistanceNm(*lat1, *lon1, *lat2, *lon2) * kNmToFt;
                const double widthM = width.value_or(0.0);

                Airport& airport = db.at(currentIcao);
                airport.runways.push_back(RunwayEnd{*id1, *lat1, *lon1, heading1, widthM, *id2, lengthFt});
                airport.runways.push_back(RunwayEnd{*id2, *lat2, *lon2, heading2, widthM, *id1, lengthFt});
            }
        } else if (rowCode == "99") {
            break; // 99 marks end-of-file in apt.dat
        }
    }

    ComputeReferencePoints(db);
    return db;
}

AirportDatabase MergeAirportDatabases(const std::vector<const AirportDatabase*>& databasesInPriorityOrder)
{
    AirportDatabase merged;
    for (const AirportDatabase* db : databasesInPriorityOrder) {
        if (!db) {
            continue;
        }
        for (const auto& [icao, airport] : *db) {
            merged.try_emplace(icao, airport); // first (highest-priority) definition wins
        }
    }
    return merged;
}

std::vector<NearbyAirport> FindNearestAirports(const AirportDatabase& db, double userLatDeg, double userLonDeg,
                                                double radiusNm)
{
    std::vector<NearbyAirport> results;
    for (const auto& [icao, airport] : db) {
        if (!airport.HasReferencePoint()) {
            continue;
        }
        const double distanceNm = GreatCircleDistanceNm(userLatDeg, userLonDeg, airport.ref_lat_deg, airport.ref_lon_deg);
        if (distanceNm <= radiusNm) {
            results.push_back(NearbyAirport{icao, airport.name, distanceNm});
        }
    }

    std::sort(results.begin(), results.end(),
              [](const NearbyAirport& a, const NearbyAirport& b) { return a.distance_nm < b.distance_nm; });
    return results;
}

std::optional<double> AirportDistanceNm(const AirportDatabase& db, const std::string& icao, double userLatDeg,
                                         double userLonDeg)
{
    const auto it = db.find(icao);
    if (it == db.end() || !it->second.HasReferencePoint()) {
        return std::nullopt;
    }
    return GreatCircleDistanceNm(userLatDeg, userLonDeg, it->second.ref_lat_deg, it->second.ref_lon_deg);
}

std::optional<double> FindRunwayLengthFt(const Airport& airport, const std::string& runwayId)
{
    for (const RunwayEnd& rwyEnd : airport.runways) {
        if (rwyEnd.id == runwayId) {
            return rwyEnd.length_ft;
        }
    }
    return std::nullopt;
}

} // namespace trm::core
