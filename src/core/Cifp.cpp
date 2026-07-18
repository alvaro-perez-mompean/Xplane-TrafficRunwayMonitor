#include "core/Cifp.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace trm::core {

namespace {

std::vector<std::string> SplitWhitespace(const std::string& text)
{
    std::vector<std::string> tokens;
    std::istringstream iss(text);
    std::string tok;
    while (iss >> tok) {
        tokens.push_back(tok);
    }
    return tokens;
}

// Comma-split keeping empty fields (e.g. ",,"  -> {"", "", ""}) -- CIFP
// lines are fixed-column, so a blank field is meaningful positionally and
// must not be dropped.
std::vector<std::string> SplitCsvKeepEmpty(const std::string& line)
{
    std::vector<std::string> tokens;
    std::string current;
    for (char c : line) {
        if (c == ',') {
            tokens.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    tokens.push_back(current);
    return tokens;
}

// Appends one leg, starting a new CifpProcedure whenever ident or
// transition differs from the last one -- see ParseCifp's own comment.
void AppendLeg(std::vector<CifpProcedure>& procs, const std::string& ident, const std::string& transition,
               const std::string& waypointId)
{
    if (procs.empty() || procs.back().ident != ident || procs.back().runway_transition != transition) {
        procs.push_back(CifpProcedure{ident, transition, {}});
    }
    procs.back().legs.push_back(CifpLeg{waypointId});
}

// tokens[2]/[3]/[4] are ident/transition/fix-ident, after the merged
// "SID:seq"/"STAR:seq"/"APPCH:seq" record-type-and-sequence token at
// tokens[0] (route-type constant at tokens[1] is unused here).
void ParseRecordLine(const std::string& line, std::vector<CifpProcedure>& procs)
{
    const std::vector<std::string> tokens = SplitCsvKeepEmpty(line);
    if (tokens.size() <= 4) {
        return;
    }
    AppendLeg(procs, tokens[2], tokens[3], tokens[4]);
}

// First letter of an approach ident (I=ILS, R=RNP, D/L/S/etc.=everything
// else) -- lower rank sorts first, so ILS is preferred when multiple
// approach types serve the same runway.
int ApproachTypeRank(const std::string& ident)
{
    if (ident.empty()) {
        return 2;
    }
    switch (ident.front()) {
        case 'I': return 0;
        case 'R': return 1;
        default: return 2;
    }
}

// Approach idents encode the runway directly: a type letter, a 2-digit
// runway number, an optional L/R/C side, and (multiple approaches of the
// same type to the same runway) an optional trailing suffix letter --
// e.g. "I32LZ" -> type 'I', runway "32L", suffix 'Z'. When the runway has
// no L/R/C side at all, real navdata inserts a literal '-' ahead of the
// suffix to keep it unambiguous ("I02-Y", confirmed at LEBL) but omits it
// when a side letter already provides the separation ("I18LY", no hyphen)
// -- both shapes are handled here.
struct ApproachIdentParts {
    char type = '\0';
    std::string runway;
    std::optional<char> suffix;
};

std::optional<ApproachIdentParts> ParseApproachIdent(const std::string& ident)
{
    if (ident.size() < 3) {
        return std::nullopt;
    }
    std::size_t i = 1; // skip the leading type letter
    const std::size_t digitsStart = i;
    while (i < ident.size() && std::isdigit(static_cast<unsigned char>(ident[i]))) {
        ++i;
    }
    if (i - digitsStart != 2) {
        return std::nullopt; // runway number is always 2 digits
    }

    ApproachIdentParts parts;
    parts.type = ident.front();
    parts.runway = ident.substr(digitsStart, 2);
    if (i < ident.size() && (ident[i] == 'L' || ident[i] == 'R' || ident[i] == 'C')) {
        parts.runway += ident[i];
        ++i;
    }
    if (i < ident.size() && ident[i] == '-') {
        ++i; // literal separator, only present without a side letter -- not data itself
    }
    if (i < ident.size()) {
        parts.suffix = ident[i];
    }
    return parts;
}

// Only the ARINC 424 approach-type letters actually confirmed against real
// navdata (LEBL/LEMD) -- see FormatApproachIdentForDisplay's own comment on
// why an unrecognized letter is left as-is rather than guessed at.
const char* ApproachTypeName(char typeLetter)
{
    switch (typeLetter) {
        case 'I': return "ILS";
        case 'L': return "LOC";
        case 'R': return "RNAV";
        case 'D': return "VOR/DME";
        case 'V': return "VOR";
        case 'N': return "NDB";
        case 'Q': return "NDB/DME";
        case 'B': return "LOC-BC";
        case 'X': return "LDA";
        default: return nullptr;
    }
}

} // namespace

CifpProcedures ParseCifp(std::istream& in)
{
    CifpProcedures result;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back(); // tolerate CRLF line endings
        }
        if (line.rfind("SID:", 0) == 0) {
            ParseRecordLine(line, result.sids);
        } else if (line.rfind("STAR:", 0) == 0) {
            ParseRecordLine(line, result.stars);
        } else if (line.rfind("APPCH:", 0) == 0) {
            ParseRecordLine(line, result.approaches);
        }
        // Other record types (RWY, PRDAT, ...) intentionally ignored.
    }
    return result;
}

bool RunwayTransitionCovers(const std::string& runwayTransition, const std::string& runwayId)
{
    if (runwayTransition.rfind("RW", 0) != 0) {
        return false; // blank, or a named enroute transition, not a runway one
    }
    const std::string suffix = runwayTransition.substr(2); // e.g. "20", "32R", "06B"
    if (suffix == runwayId) {
        return true;
    }
    if (!suffix.empty() && suffix.back() == 'B') {
        const std::string base = suffix.substr(0, suffix.size() - 1);
        return runwayId == base + "L" || runwayId == base + "R" || runwayId == base;
    }
    return false;
}

std::vector<std::string> FindSidsForRunwayFix(const std::vector<CifpProcedure>& sids, const std::string& runwayId,
                                                const std::string& waypointId)
{
    std::vector<std::string> idents;
    if (waypointId.empty()) {
        return idents;
    }
    for (const CifpProcedure& proc : sids) {
        if (proc.legs.empty() || proc.legs.back().waypoint_id != waypointId) {
            continue;
        }
        if (!RunwayTransitionCovers(proc.runway_transition, runwayId)) {
            continue;
        }
        if (std::find(idents.begin(), idents.end(), proc.ident) == idents.end()) {
            idents.push_back(proc.ident);
        }
    }
    return idents;
}

std::vector<std::string> FindStarsForFix(const std::vector<CifpProcedure>& stars, const std::string& waypointId)
{
    std::vector<std::string> idents;
    if (waypointId.empty()) {
        return idents;
    }
    for (const CifpProcedure& proc : stars) {
        if (proc.legs.empty() || proc.legs.front().waypoint_id != waypointId) {
            continue;
        }
        if (std::find(idents.begin(), idents.end(), proc.ident) == idents.end()) {
            idents.push_back(proc.ident);
        }
    }
    return idents;
}

std::vector<std::string> FindApproachesForRunway(const std::vector<CifpProcedure>& approaches,
                                                    const std::string& runwayId)
{
    std::vector<std::string> idents;
    for (const CifpProcedure& proc : approaches) {
        const std::optional<ApproachIdentParts> parts = ParseApproachIdent(proc.ident);
        if (!parts.has_value() || parts->runway != runwayId) {
            continue;
        }
        if (std::find(idents.begin(), idents.end(), proc.ident) == idents.end()) {
            idents.push_back(proc.ident);
        }
    }
    std::stable_sort(idents.begin(), idents.end(), [](const std::string& a, const std::string& b) {
        return ApproachTypeRank(a) < ApproachTypeRank(b);
    });
    return idents;
}

std::string FormatApproachIdentForDisplay(const std::string& ident)
{
    const std::optional<ApproachIdentParts> parts = ParseApproachIdent(ident);
    if (!parts.has_value()) {
        return ident;
    }
    const char* typeName = ApproachTypeName(parts->type);
    std::string result = typeName != nullptr ? typeName : std::string(1, parts->type);
    result += parts->runway;
    if (parts->suffix.has_value()) {
        result += '-';
        result += *parts->suffix;
    }
    return result;
}

std::optional<std::string> ExtractDepartureAnchorFix(const std::string& rawRoute,
                                                        const std::vector<CifpProcedure>& sids,
                                                        const std::string& runwayId)
{
    const std::vector<std::string> tokens = SplitWhitespace(rawRoute);
    if (tokens.empty()) {
        return std::nullopt;
    }
    if (tokens.size() >= 2 && !FindSidsForRunwayFix(sids, runwayId, tokens[1]).empty()) {
        return tokens[1];
    }
    if (!FindSidsForRunwayFix(sids, runwayId, tokens[0]).empty()) {
        return tokens[0]; // no SID assigned -- the first token is already a real fix
    }
    // No CIFP-confirmed match either way -- best-effort fallback assuming
    // Simbrief's usual "SID FIX ..." shape.
    return tokens.size() >= 2 ? tokens[1] : tokens[0];
}

std::optional<std::string> ExtractArrivalAnchorFix(const std::string& rawRoute,
                                                       const std::vector<CifpProcedure>& stars)
{
    const std::vector<std::string> tokens = SplitWhitespace(rawRoute);
    if (tokens.empty()) {
        return std::nullopt;
    }
    if (tokens.size() >= 2) {
        const std::string& secondToLast = tokens[tokens.size() - 2];
        if (!FindStarsForFix(stars, secondToLast).empty()) {
            return secondToLast;
        }
    }
    if (!FindStarsForFix(stars, tokens.back()).empty()) {
        return tokens.back(); // no STAR assigned -- the last token is already a real fix
    }
    return tokens.size() >= 2 ? tokens[tokens.size() - 2] : tokens.back();
}

std::string FormatProcedureSummary(const std::string& originIcao, const std::optional<std::string>& departureRunway,
                                     const std::optional<std::string>& sid, const std::optional<std::string>& star,
                                     const std::string& destinationIcao,
                                     const std::optional<std::string>& arrivalRunway,
                                     const std::optional<std::string>& approach)
{
    std::string result = originIcao;
    if (departureRunway.has_value()) {
        result += "/" + *departureRunway;
    }
    if (sid.has_value()) {
        result += " " + *sid;
    }
    if (star.has_value()) {
        result += "  " + *star;
    }
    result += " " + destinationIcao;
    if (arrivalRunway.has_value()) {
        result += "/" + *arrivalRunway;
    }
    if (approach.has_value()) {
        result += " " + *approach;
    }
    return result;
}

} // namespace trm::core
