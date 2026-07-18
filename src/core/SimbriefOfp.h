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
    // LIDO-style route line assembled from Simbrief's own fields, e.g.
    // "LEBL/20 SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R" --
    // display-only informational text, never fed into the
    // origin/destination override plumbing (a planned runway is tentative
    // pre-departure, unlike a confirmed origin/destination ICAO). Nullopt
    // if origin/destination ICAO or the enroute route string itself
    // couldn't be extracted; a missing planned runway on either end just
    // drops that airport's "/RWY" suffix rather than nulling the whole
    // thing out.
    std::optional<std::string> route_text;
};

// Extracts "icao_code" out of the top-level "origin"/"destination" objects,
// and "route"/"plan_rwy" out of "general"/"origin"/"destination", in raw
// Simbrief OFP JSON. Two-stage lookup per field -- first scopes to the
// object body (`"origin":{...}`) via a manual, depth-counting brace scan
// (correctly skips arbitrarily nested `{...}` and quoted strings inside,
// e.g. Simbrief's real "general" object has empty placeholders like
// "sys_rmk":{} ahead of "route"), then searches within that scoped
// substring for the field -- so key reordering within the object doesn't
// break extraction. Deliberately not std::regex: an earlier regex-based
// version of this scan worked fine against small hand-written test
// fixtures but threw against Simbrief's real, full-size OFP JSON (hundreds
// of KB) -- this manual scan is linear in input size with no backtracking,
// so it can't misbehave the same way. Any field is nullopt if not found or
// the JSON is malformed -- never throws.
SimbriefOriginDestination ParseSimbriefOfp(const std::string& json);

} // namespace trm::core
