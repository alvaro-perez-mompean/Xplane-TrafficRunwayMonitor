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
    // Simbrief's own planned runway for each end ("plan_rwy") -- same
    // tentative, pre-departure caveat as route_text above. Used only to
    // seed the Flight Plan tab's departure/arrival runway selector's
    // initial value (core::Cifp.h), never treated as confirmed. Nullopt if
    // Simbrief hadn't assigned one yet.
    std::optional<std::string> origin_planned_runway;
    std::optional<std::string> destination_planned_runway;
    // The raw "general.route" field verbatim -- SID/airway/waypoint/STAR
    // tokens only, no ICAO/runway prefixes (route_text above is this same
    // string embedded in a LIDO-style display line). Kept separately so
    // core::ExtractDepartureAnchorFix/ExtractArrivalAnchorFix (Cifp.h) can
    // tokenize it directly instead of having to strip route_text's own
    // formatting back off.
    std::optional<std::string> raw_route;
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

// Simbrief OFP fuel figures, extracted from the top-level "fuel" object
// (plus "units" out of "params") -- see ParseSimbriefFuelPlan. Quantities
// are in whatever unit Simbrief itself used to build the OFP (kgs or lbs,
// per `units`) -- this plugin does no conversion. A field is nullopt only
// when it's missing/malformed in the response, never to represent a
// genuine zero (e.g. no extra fuel carried is a real "0", not "absent") --
// callers should render nullopt fields as simply absent from the table,
// not as blank/zero.
struct SimbriefFuelPlan {
    std::optional<std::string> units; // "kgs" or "lbs", verbatim from Simbrief
    std::optional<long long> taxi;
    std::optional<long long> trip;         // "enroute_burn"
    std::optional<long long> contingency;
    std::optional<long long> alternate;    // "alternate_burn"
    std::optional<long long> reserve;      // final reserve
    std::optional<long long> extra;
    std::optional<long long> block;        // "plan_ramp" -- total fuel at pushback
    std::optional<long long> max_tanks;    // total usable fuel tank capacity
};

// Same manual, non-throwing object/field scan as ParseSimbriefOfp (see its
// own comment for why -- std::regex choked on Simbrief's real, full-size
// responses); reused here instead of duplicated. Any field is nullopt if
// not found or malformed; never throws.
SimbriefFuelPlan ParseSimbriefFuelPlan(const std::string& json);

// Simbrief OFP weight figures (planned/estimated + structural max), from
// the top-level "weights" object (plus "units" out of "params", same
// convention as SimbriefFuelPlan). No "actual" figures -- unlike a real
// LIDO OFP's blank ACTUAL column (hand-filled from the final loadsheet),
// this plugin has no live weight source to put there, so it's omitted
// entirely rather than rendered as a permanently-empty placeholder.
struct SimbriefWeights {
    std::optional<std::string> units;
    std::optional<long long> pax_count;    // a headcount, not a weight -- never tonnes-formatted
    std::optional<long long> cargo;
    std::optional<long long> payload;
    std::optional<long long> zfw_est;      // "est_zfw"
    std::optional<long long> zfw_max;      // "max_zfw"
    std::optional<long long> tow_est;      // "est_tow"
    std::optional<long long> tow_max;      // "max_tow"
    std::optional<long long> law_est;      // "est_ldw" -- estimated landing weight
    std::optional<long long> law_max;      // "max_ldw"
};

// Same manual, non-throwing object/field scan as ParseSimbriefOfp. Any
// field is nullopt if not found or malformed; never throws.
SimbriefWeights ParseSimbriefWeights(const std::string& json);

// Formats a raw Simbrief weight quantity (already in `units`, kgs/lbs) the
// way a LIDO-style OFP displays it: tonnes to one decimal for kgs (e.g.
// 219300 -> "219.3", matching how Simbrief's own generated OFPs display
// metric weights), the raw whole-unit figure for lbs or an unrecognized/
// missing unit (no tonnes convention for pounds).
std::string FormatSimbriefWeightTonnes(long long quantity, const std::optional<std::string>& units);

// Simbrief OFP header/identity figures, from the top-level "general"/
// "aircraft"/"alternate"/"times"/"params" objects -- see
// ParseSimbriefHeader. Same nullopt convention as the other Simbrief
// structs above (missing/malformed -> nullopt, never a fabricated value).
struct SimbriefHeader {
    std::optional<std::string> callsign;           // "icao_airline" + "flight_number", e.g. "IB2301"
    std::optional<std::string> aircraft_type;      // aircraft.icaocode
    std::optional<std::string> aircraft_reg;       // aircraft.reg
    std::optional<std::string> cost_index;         // general.costindex -- a label, not a quantity to compute with
    std::optional<std::string> departure_date;     // "DDMMMYYYY" UTC, from times.sched_out
    std::optional<std::string> release_id;         // general.release
    std::optional<std::string> release_date;       // "DDMMMYY" UTC, from params.time_generated
    std::optional<std::string> alternate_icao;     // alternate.icao_code
    std::optional<std::string> step_climbs;        // general.stepclimb_string, verbatim (e.g. "LEBL/0290")
    std::optional<std::string> avg_wind_component; // signed, zero-padded "P040"/"M038" -- see FormatSignedThreeDigit
    std::optional<std::string> avg_isa_deviation;  // same "P"/"M" convention, from general.avg_temp_dev
};

// Same manual, non-throwing object/field scan as ParseSimbriefOfp. Any
// field is nullopt if not found or malformed; never throws.
SimbriefHeader ParseSimbriefHeader(const std::string& json);

} // namespace trm::core
