#include "core/SimbriefOfp.h"

#include <cctype>
#include <charconv>
#include <cstdio>

namespace trm::core {

namespace {

// Scans forward from `pos` (which must point at a JSON string literal's
// opening '"') past its closing '"', honoring backslash escapes so an
// escaped quote (`\"`) doesn't end the string early. Returns the index
// just past the closing '"', or std::string::npos if unterminated.
std::size_t SkipJsonString(const std::string& text, std::size_t pos)
{
    ++pos; // past the opening quote
    while (pos < text.size()) {
        if (text[pos] == '\\') {
            pos += 2; // skip the escaped character too, whatever it is
            continue;
        }
        if (text[pos] == '"') {
            return pos + 1;
        }
        ++pos;
    }
    return std::string::npos;
}

std::size_t SkipWhitespace(const std::string& text, std::size_t pos)
{
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos;
}

// Finds `"objectKey":{...}` in `text` and returns the substring between
// (not including) its braces -- a manual, depth-counting scan, so it
// handles arbitrarily nested `{...}`/quoted strings inside correctly
// (unlike a bare `[^}]*`-style capture). Skips over quoted string
// contents while counting depth so a brace character inside a string
// value doesn't miscount. Previously implemented as a backtracking
// std::regex; that choked on Simbrief's real, full-size OFP JSON
// (hundreds of KB) in a way none of this file's small unit-test fixtures
// ever exercised -- this manual scan is linear in the input size with no
// backtracking, so it can't misbehave the same way regardless of input
// size or shape.
std::optional<std::string> ExtractObjectBody(const std::string& text, const std::string& objectKey)
{
    const std::string needle = "\"" + objectKey + "\"";
    const std::size_t keyPos = text.find(needle);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t pos = SkipWhitespace(text, keyPos + needle.size());
    if (pos >= text.size() || text[pos] != ':') {
        return std::nullopt;
    }
    pos = SkipWhitespace(text, pos + 1);
    if (pos >= text.size() || text[pos] != '{') {
        return std::nullopt;
    }

    const std::size_t bodyStart = pos + 1;
    int depth = 1;
    std::size_t i = bodyStart;
    while (i < text.size() && depth > 0) {
        if (text[i] == '"') {
            const std::size_t afterString = SkipJsonString(text, i);
            if (afterString == std::string::npos) {
                return std::nullopt;
            }
            i = afterString;
            continue;
        }
        if (text[i] == '{') {
            ++depth;
        } else if (text[i] == '}') {
            --depth;
        }
        ++i;
    }
    if (depth != 0) {
        return std::nullopt; // unterminated object
    }
    return text.substr(bodyStart, (i - 1) - bodyStart);
}

// Un-escapes a JSON string body (already stripped of its surrounding
// quotes): the standard backslash escapes (`\"` `\\` `\/` `\b` `\f` `\n`
// `\r` `\t`) decoded, anything else copied through verbatim rather than
// guessed at. Simbrief escapes forward slashes in its own output (confirmed
// in a real response's "stepclimb_string":"LEBL\/0290") -- earlier callers
// never needed this since ICAO codes/plan_rwy/route text never contained a
// literal "/" to begin with, but a field like stepclimb_string does.
// Deliberately no `\uXXXX` handling: every Simbrief field this file reads
// is plain ASCII in practice, so it's not worth the extra Unicode-decoding
// surface area for a case that's never been observed.
std::string UnescapeJsonString(const std::string& raw)
{
    std::string result;
    result.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\' || i + 1 >= raw.size()) {
            result += raw[i];
            continue;
        }
        ++i;
        switch (raw[i]) {
            case '"': result += '"'; break;
            case '\\': result += '\\'; break;
            case '/': result += '/'; break;
            case 'b': result += '\b'; break;
            case 'f': result += '\f'; break;
            case 'n': result += '\n'; break;
            case 'r': result += '\r'; break;
            case 't': result += '\t'; break;
            default:
                result += '\\';
                result += raw[i];
                break;
        }
    }
    return result;
}

// Finds `"fieldName":"value"` within `objectBody` and returns `value`,
// unescaped (see UnescapeJsonString above). Nullopt if the key or its
// quoted value isn't found.
std::optional<std::string> ExtractQuotedField(const std::string& objectBody, const std::string& fieldName)
{
    const std::string needle = "\"" + fieldName + "\"";
    const std::size_t keyPos = objectBody.find(needle);
    if (keyPos == std::string::npos) {
        return std::nullopt;
    }

    std::size_t pos = SkipWhitespace(objectBody, keyPos + needle.size());
    if (pos >= objectBody.size() || objectBody[pos] != ':') {
        return std::nullopt;
    }
    pos = SkipWhitespace(objectBody, pos + 1);
    if (pos >= objectBody.size() || objectBody[pos] != '"') {
        return std::nullopt;
    }

    const std::size_t valueStart = pos + 1;
    const std::size_t afterValue = SkipJsonString(objectBody, pos);
    if (afterValue == std::string::npos) {
        return std::nullopt;
    }
    return UnescapeJsonString(objectBody.substr(valueStart, (afterValue - 1) - valueStart));
}

std::optional<std::string> ExtractIcaoCode(const std::string& objectBody)
{
    const std::optional<std::string> value = ExtractQuotedField(objectBody, "icao_code");
    if (!value.has_value() || value->size() != 4) {
        return std::nullopt;
    }
    for (unsigned char c : *value) {
        if (!std::isupper(c) && !std::isdigit(c)) {
            return std::nullopt;
        }
    }
    return value;
}

// General string-valued field lookup, e.g. plan_rwy ("20", "32R") or route
// (the space-separated SID/airway/STAR string). An empty value (Simbrief
// leaves plan_rwy blank until a runway's assigned) is treated the same as
// "field not found".
std::optional<std::string> ExtractStringField(const std::string& objectBody, const std::string& fieldName)
{
    const std::optional<std::string> value = ExtractQuotedField(objectBody, fieldName);
    if (!value.has_value() || value->empty()) {
        return std::nullopt;
    }
    return value;
}

std::string FormatAirportRoutePoint(const std::string& icao, const std::optional<std::string>& plannedRunway)
{
    return plannedRunway.has_value() ? (icao + "/" + *plannedRunway) : icao;
}

// Integer-valued field lookup (Simbrief's own fuel figures are quoted
// strings of digits, e.g. "12345"). std::from_chars rather than std::stoll
// -- never throws, and rejects trailing garbage (`result.ptr != end`)
// instead of silently accepting a leading-numeric-prefix match.
std::optional<long long> ExtractIntegerField(const std::string& objectBody, const std::string& fieldName)
{
    const std::optional<std::string> value = ExtractQuotedField(objectBody, fieldName);
    if (!value.has_value() || value->empty()) {
        return std::nullopt;
    }
    long long parsed = 0;
    const char* begin = value->data();
    const char* end = value->data() + value->size();
    const auto conversion = std::from_chars(begin, end, parsed);
    if (conversion.ec != std::errc() || conversion.ptr != end) {
        return std::nullopt;
    }
    return parsed;
}

// Signed-quantity field lookup, formatted the way a LIDO OFP shows average
// wind component / ISA deviation: a "P"/"M" sign letter (plus/minus) and
// the zero-padded 3-digit magnitude, e.g. -38 -> "M038", 10 -> "P010".
std::optional<std::string> ExtractSignedThreeDigit(const std::string& objectBody, const std::string& fieldName)
{
    const std::optional<long long> value = ExtractIntegerField(objectBody, fieldName);
    if (!value.has_value()) {
        return std::nullopt;
    }
    const long long magnitude = *value < 0 ? -*value : *value;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%c%03lld", *value < 0 ? 'M' : 'P', magnitude);
    return std::string(buf);
}

constexpr const char* kMonthAbbrev[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                           "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};

// Days-since-1970-01-01 -> proleptic Gregorian (year, month, day), valid for
// any input. Howard Hinnant's civil_from_days algorithm
// (http://howardhinnant.github.io/date_algorithms.html) -- pure integer
// math, deliberately not <ctime>'s gmtime: ParseSimbriefHeader can run on a
// background fetch thread, and gmtime isn't reliably thread-safe/portable
// for that (glibc's version writes through a single shared static buffer).
void CivilFromDays(long long z, int& year, int& month, int& day)
{
    z += 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(z - era * 146097);           // [0, 146096]
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    const long long y = static_cast<long long>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);           // [0, 365]
    const unsigned mp = (5 * doy + 2) / 153;                                // [0, 11]
    day = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);                   // [1, 31]
    month = static_cast<int>(mp + (mp < 10 ? 3 : -9));                      // [1, 12]
    year = static_cast<int>(y + (month <= 2 ? 1 : 0));
}

// Formats a Unix epoch (UTC) as a LIDO-style date: "DDMMMYYYY" if
// `fourDigitYear`, else "DDMMMYY" (matches the header line's departure date
// vs. the release line's shorter date, e.g. "08JUN2015" vs. "07JUN15").
std::string FormatOfpDate(long long epochSeconds, bool fourDigitYear)
{
    const long long days = epochSeconds >= 0 ? epochSeconds / 86400 : (epochSeconds - 86399) / 86400;
    int year = 0;
    int month = 0;
    int day = 0;
    CivilFromDays(days, year, month, day);
    if (month < 1 || month > 12) {
        return {};
    }
    char buf[16];
    if (fourDigitYear) {
        std::snprintf(buf, sizeof(buf), "%02d%s%04d", day, kMonthAbbrev[month - 1], year);
    } else {
        std::snprintf(buf, sizeof(buf), "%02d%s%02d", day, kMonthAbbrev[month - 1], year % 100);
    }
    return buf;
}

} // namespace

SimbriefOriginDestination ParseSimbriefOfp(const std::string& json)
{
    SimbriefOriginDestination result;

    const std::optional<std::string> originBody = ExtractObjectBody(json, "origin");
    const std::optional<std::string> destinationBody = ExtractObjectBody(json, "destination");
    result.origin_icao = originBody ? ExtractIcaoCode(*originBody) : std::nullopt;
    result.destination_icao = destinationBody ? ExtractIcaoCode(*destinationBody) : std::nullopt;

    const std::optional<std::string> generalBody = ExtractObjectBody(json, "general");
    const std::optional<std::string> route = generalBody ? ExtractStringField(*generalBody, "route") : std::nullopt;
    if (result.origin_icao && result.destination_icao && route) {
        const std::optional<std::string> originRwy =
            originBody ? ExtractStringField(*originBody, "plan_rwy") : std::nullopt;
        const std::optional<std::string> destinationRwy =
            destinationBody ? ExtractStringField(*destinationBody, "plan_rwy") : std::nullopt;
        result.route_text = FormatAirportRoutePoint(*result.origin_icao, originRwy) + " " + *route + " " +
                             FormatAirportRoutePoint(*result.destination_icao, destinationRwy);
    }

    return result;
}

SimbriefFuelPlan ParseSimbriefFuelPlan(const std::string& json)
{
    SimbriefFuelPlan result;

    const std::optional<std::string> paramsBody = ExtractObjectBody(json, "params");
    result.units = paramsBody ? ExtractStringField(*paramsBody, "units") : std::nullopt;

    const std::optional<std::string> fuelBody = ExtractObjectBody(json, "fuel");
    if (!fuelBody.has_value()) {
        return result;
    }
    result.taxi = ExtractIntegerField(*fuelBody, "taxi");
    result.trip = ExtractIntegerField(*fuelBody, "enroute_burn");
    result.contingency = ExtractIntegerField(*fuelBody, "contingency");
    result.alternate = ExtractIntegerField(*fuelBody, "alternate_burn");
    result.reserve = ExtractIntegerField(*fuelBody, "reserve");
    result.extra = ExtractIntegerField(*fuelBody, "extra");
    result.block = ExtractIntegerField(*fuelBody, "plan_ramp");
    result.max_tanks = ExtractIntegerField(*fuelBody, "max_tanks");

    return result;
}

SimbriefWeights ParseSimbriefWeights(const std::string& json)
{
    SimbriefWeights result;

    const std::optional<std::string> paramsBody = ExtractObjectBody(json, "params");
    result.units = paramsBody ? ExtractStringField(*paramsBody, "units") : std::nullopt;

    const std::optional<std::string> weightsBody = ExtractObjectBody(json, "weights");
    if (!weightsBody.has_value()) {
        return result;
    }
    result.pax_count = ExtractIntegerField(*weightsBody, "pax_count");
    result.cargo = ExtractIntegerField(*weightsBody, "cargo");
    result.payload = ExtractIntegerField(*weightsBody, "payload");
    result.zfw_est = ExtractIntegerField(*weightsBody, "est_zfw");
    result.zfw_max = ExtractIntegerField(*weightsBody, "max_zfw");
    result.tow_est = ExtractIntegerField(*weightsBody, "est_tow");
    result.tow_max = ExtractIntegerField(*weightsBody, "max_tow");
    result.law_est = ExtractIntegerField(*weightsBody, "est_ldw");
    result.law_max = ExtractIntegerField(*weightsBody, "max_ldw");

    return result;
}

std::string FormatSimbriefWeightTonnes(long long quantity, const std::optional<std::string>& units)
{
    char buf[32];
    if (units.has_value() && *units == "kgs") {
        std::snprintf(buf, sizeof(buf), "%.1f", quantity / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%lld", quantity);
    }
    return buf;
}

SimbriefHeader ParseSimbriefHeader(const std::string& json)
{
    SimbriefHeader result;

    const std::optional<std::string> generalBody = ExtractObjectBody(json, "general");
    if (generalBody.has_value()) {
        const std::optional<std::string> airline = ExtractStringField(*generalBody, "icao_airline");
        const std::optional<std::string> flightNumber = ExtractStringField(*generalBody, "flight_number");
        if (airline.has_value() && flightNumber.has_value()) {
            result.callsign = *airline + *flightNumber;
        }
        result.cost_index = ExtractStringField(*generalBody, "costindex");
        result.release_id = ExtractStringField(*generalBody, "release");
        result.step_climbs = ExtractStringField(*generalBody, "stepclimb_string");
        result.avg_wind_component = ExtractSignedThreeDigit(*generalBody, "avg_wind_comp");
        result.avg_isa_deviation = ExtractSignedThreeDigit(*generalBody, "avg_temp_dev");
    }

    const std::optional<std::string> aircraftBody = ExtractObjectBody(json, "aircraft");
    if (aircraftBody.has_value()) {
        result.aircraft_type = ExtractStringField(*aircraftBody, "icaocode");
        result.aircraft_reg = ExtractStringField(*aircraftBody, "reg");
    }

    const std::optional<std::string> alternateBody = ExtractObjectBody(json, "alternate");
    result.alternate_icao = alternateBody ? ExtractIcaoCode(*alternateBody) : std::nullopt;

    const std::optional<std::string> timesBody = ExtractObjectBody(json, "times");
    const std::optional<long long> schedOut = timesBody ? ExtractIntegerField(*timesBody, "sched_out") : std::nullopt;
    if (schedOut.has_value()) {
        result.departure_date = FormatOfpDate(*schedOut, /*fourDigitYear=*/true);
    }

    const std::optional<std::string> paramsBody = ExtractObjectBody(json, "params");
    const std::optional<long long> generated =
        paramsBody ? ExtractIntegerField(*paramsBody, "time_generated") : std::nullopt;
    if (generated.has_value()) {
        result.release_date = FormatOfpDate(*generated, /*fourDigitYear=*/false);
    }

    return result;
}

} // namespace trm::core
