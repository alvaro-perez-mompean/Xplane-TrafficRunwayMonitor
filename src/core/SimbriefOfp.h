#pragma once

#include <optional>
#include <string>

// Pure parsing of a Simbrief OFP (flight plan) JSON response
// (api/xml.fetcher.php?userid=<id>&json=1) into origin/destination ICAO
// codes. Zero XPLM dependency -- see sdk/SimbriefClient.h for the real
// HTTP fetch that produces this raw JSON text.

namespace trm::core {

struct SimbriefOriginDestination {
    std::optional<std::string> origin_icao;
    std::optional<std::string> destination_icao;
};

// Extracts "icao_code" out of the top-level "origin"/"destination" objects
// in raw Simbrief OFP JSON. Two-stage regex per field -- first scopes to
// the object body (`"origin":{...}`), then searches within that captured
// substring for icao_code -- so key reordering within the object doesn't
// break extraction. Known limitation: a nested `{...}` object appearing
// before icao_code inside origin/destination would break the outer,
// brace-matching-free capture; Simbrief's schema doesn't currently do this
// for these two fields. Either or both fields are nullopt if not found or
// the JSON is malformed -- never throws.
SimbriefOriginDestination ParseSimbriefOfp(const std::string& json);

} // namespace trm::core
