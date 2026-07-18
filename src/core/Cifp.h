#pragma once

#include <istream>
#include <optional>
#include <string>
#include <vector>

// Parsing and matching for X-Plane's per-airport CIFP terminal-procedure
// data (SID/STAR/approach), used by the Flight Plan tab's Procedures
// section to recommend a departure SID, arrival STAR, and approach that
// actually match the runway/route in use -- Simbrief's own OFP only ever
// gives a tentative runway guess, not necessarily the active one.
//
// Pure logic, no filesystem access: ParseCifp takes an already-open stream
// so it's directly unit-testable against a synthetic fixture, same as
// core::AptDat. Locating and opening the real per-airport CIFP file is
// sdk::CifpLoader's job, not this module's.

namespace trm::core {

// One leg of a procedure. Only the waypoint identifier matters for the
// matching this module does -- leg geometry (course/altitude/speed
// constraints) is deliberately left unparsed, since procedure *selection*
// never needs it. Empty for legs with no fix at all (e.g. a course-to-
// altitude "CA" leg).
struct CifpLeg {
    std::string waypoint_id;
};

struct CifpProcedure {
    std::string ident;             // e.g. "SENI2J", "ADUX3D", "I32LZ"
    std::string runway_transition; // e.g. "RW20", "RW06B" (both-ends), or a named enroute transition/blank
    std::vector<CifpLeg> legs;      // in file order
};

struct CifpProcedures {
    std::vector<CifpProcedure> sids;
    std::vector<CifpProcedure> stars;
    std::vector<CifpProcedure> approaches;
};

// Parses "SID:"/"STAR:"/"APPCH:" lines out of an open CIFP .dat stream.
// Each line is a comma-separated record; only ident (field 3), runway/
// transition (field 4), and leg fix ident (field 5) are read -- the rest
// (path terminator, altitude/speed constraints, etc.) is irrelevant to
// procedure selection. Consecutive lines sharing the same ident+transition
// are one procedure's legs, in file order -- X-Plane's CIFP format has no
// other grouping marker. Any other record type (RWY, PRDAT, ...) is
// ignored. Never throws; a malformed line is silently skipped.
CifpProcedures ParseCifp(std::istream& in);

// True if a procedure's runway_transition covers `runwayId`: an exact match
// ("RW20" vs "20"), or the ARINC 424 "both members of a parallel pair"
// convention ("RW06B" covers both "06L" and "06R"). False for a blank or
// non-runway (named enroute) transition.
bool RunwayTransitionCovers(const std::string& runwayTransition, const std::string& runwayId);

// Every SID whose runway transition covers `runwayId` and whose last leg
// (the common/enroute-release fix a runway-specific SID transition always
// terminates at) is `waypointId`. Returns idents only -- `waypointId` is
// already known to the caller. Empty if the airport has no CIFP SID data,
// or no SID reaches that fix from that runway.
std::vector<std::string> FindSidsForRunwayFix(const std::vector<CifpProcedure>& sids, const std::string& runwayId,
                                                const std::string& waypointId);

// Every STAR whose first leg (the named enroute-transition entry fix a
// STAR's descent-path group always starts at) is `waypointId` --
// deliberately runway-independent: a STAR's runway-specific tail only
// matters once an approach is chosen (see FindApproachesForRunway below),
// not for picking the STAR itself.
std::vector<std::string> FindStarsForFix(const std::vector<CifpProcedure>& stars, const std::string& waypointId);

// Every approach whose ident directly names `runwayId`. Unlike SID/STAR,
// X-Plane/ARINC 424 approach idents encode the runway in the string itself
// (a type letter, then a 2-digit runway number, then an optional L/R/C
// side, then an optional suffix letter distinguishing multiple approaches
// of the same type to the same runway -- e.g. "I32LZ", "I32LW", "R32LY"),
// so no fix-anchoring is needed. Sorted most-precise-first (ILS, then RNP,
// then everything else) so the first candidate is a reasonable default.
std::vector<std::string> FindApproachesForRunway(const std::vector<CifpProcedure>& approaches,
                                                    const std::string& runwayId);

// Reformats a raw CIFP approach ident into the same shape a real FMS/MCDU
// shows (confirmed against a ToLiss Airbus MCDU): the type letter spelled
// out, and a same-runway/same-type suffix letter separated with a hyphen --
// e.g. "I32RW" -> "ILS32R-W", "D18L" -> "VOR/DME18L". Only the well-
// established ARINC 424 type letters actually confirmed against real
// navdata are spelled out (ILS/LOC/RNAV/VOR/NDB/NDB-DME/LOC-BC/LDA); an
// unrecognized type letter is kept as-is rather than guessed at, same
// non-guessing convention as core::SimbriefOfp's UnescapeJsonString. Falls
// back to the bare ident unmodified if it doesn't match the expected
// type-letter+2digit-runway[+side][-][+suffix] shape at all.
std::string FormatApproachIdentForDisplay(const std::string& ident);

// Picks the departure anchor fix out of Simbrief's raw route string (see
// core::SimbriefOriginDestination::raw_route) -- normally the second token
// ("SENIA2J SENIA ..." -> "SENIA"), Simbrief's own SID name being the
// first. Falls back to the first token itself if no SID for `runwayId`
// reaches the second token (handles the rarer case of a route with no SID
// assigned at all, where the first token is already a real fix). Best-
// effort if CIFP data can't confirm either reading. Nullopt only if
// rawRoute is empty.
std::optional<std::string> ExtractDepartureAnchorFix(const std::string& rawRoute,
                                                        const std::vector<CifpProcedure>& sids,
                                                        const std::string& runwayId);

// Mirror of ExtractDepartureAnchorFix for the arrival end: picks the
// second-to-last token ("... ADUXO ADUXO3D" -> "ADUXO"), Simbrief's own
// STAR name being the last. Same no-STAR-assigned fallback and best-effort
// caveat as the departure side.
std::optional<std::string> ExtractArrivalAnchorFix(const std::string& rawRoute,
                                                       const std::vector<CifpProcedure>& stars);

// Formats the Flight Plan tab's "Using" line from the current procedure
// selections -- deliberately independent of (and never written back into)
// Simbrief's own route_text, which stays a historical record of what
// Simbrief planned rather than what's actually being flown. Missing pieces
// are simply omitted.
std::string FormatProcedureSummary(const std::string& originIcao, const std::optional<std::string>& departureRunway,
                                     const std::optional<std::string>& sid, const std::optional<std::string>& star,
                                     const std::string& destinationIcao,
                                     const std::optional<std::string>& arrivalRunway,
                                     const std::optional<std::string>& approach);

} // namespace trm::core
