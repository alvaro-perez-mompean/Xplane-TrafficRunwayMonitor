#include "core/SimbriefOfp.h"

#include <cctype>

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

// Finds `"fieldName":"value"` within `objectBody` and returns `value`
// verbatim (no unescaping -- none of this file's callers need it, since
// ICAO codes and route/runway text are plain ASCII). Nullopt if the key
// or its quoted value isn't found.
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
    return objectBody.substr(valueStart, (afterValue - 1) - valueStart);
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

} // namespace trm::core
