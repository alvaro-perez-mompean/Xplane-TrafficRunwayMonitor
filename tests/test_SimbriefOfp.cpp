#include <catch2/catch_test_macros.hpp>

#include "core/SimbriefOfp.h"

using namespace trm::core;

TEST_CASE("ParseSimbriefOfp: well-formed JSON extracts both ICAOs", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"KJFK","iata_code":"JFK"},"destination":{"icao_code":"KLAX","iata_code":"LAX"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    REQUIRE(result.destination_icao.has_value());
    CHECK(*result.origin_icao == "KJFK");
    CHECK(*result.destination_icao == "KLAX");
}

TEST_CASE("ParseSimbriefOfp: missing destination key yields nullopt for that field only", "[SimbriefOfp]")
{
    const std::string json = R"({"origin":{"icao_code":"KJFK"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    CHECK(*result.origin_icao == "KJFK");
    CHECK_FALSE(result.destination_icao.has_value());
}

TEST_CASE("ParseSimbriefOfp: reordered keys within the object still parse", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"iata_code":"JFK","name":"John F Kennedy","icao_code":"KJFK"},)"
        R"("destination":{"icao_code":"KLAX","name":"Los Angeles Intl"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    REQUIRE(result.destination_icao.has_value());
    CHECK(*result.origin_icao == "KJFK");
    CHECK(*result.destination_icao == "KLAX");
}

TEST_CASE("ParseSimbriefOfp: malformed/truncated JSON yields both nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefOfp("not json at all");
    CHECK_FALSE(result.origin_icao.has_value());
    CHECK_FALSE(result.destination_icao.has_value());
}

TEST_CASE("ParseSimbriefOfp: empty string yields both nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefOfp("");
    CHECK_FALSE(result.origin_icao.has_value());
    CHECK_FALSE(result.destination_icao.has_value());
}

TEST_CASE("ParseSimbriefOfp: assembles a LIDO-style route line with both planned runways", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":"20"},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL/20 SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: route line omits a runway suffix that hasn't been planned yet", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":""},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: route line is nullopt when general.route is missing", "[SimbriefOfp]")
{
    const std::string json = R"({"origin":{"icao_code":"LEBL"},"destination":{"icao_code":"LEMD"}})";
    const auto result = ParseSimbriefOfp(json);
    CHECK_FALSE(result.route_text.has_value());
}

TEST_CASE("ParseSimbriefOfp: route line is nullopt when an ICAO is missing even if route is present", "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL"},"general":{"route":"SENIA2J SENIA Z596"}})";
    const auto result = ParseSimbriefOfp(json);
    CHECK_FALSE(result.route_text.has_value());
}

TEST_CASE("ParseSimbriefOfp: route survives an empty nested object ahead of it in general (real Simbrief shape)",
          "[SimbriefOfp]")
{
    // Simbrief's actual "general" object puts "sys_rmk":{} (and other empty
    // placeholder objects) before "route" -- a naive `[^}]*` object-body
    // capture stops at sys_rmk's own closing brace and never reaches route.
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":"20"},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"icao_airline":"IBE","sys_rmk":{},"route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D",)"
        R"("route_track_replace":{}}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL/20 SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: route survives multiple levels of nested objects and arrays ahead of it",
          "[SimbriefOfp]")
{
    // A non-empty, multi-level-deep nested object/array (not just Simbrief's
    // own "sys_rmk":{}) ahead of "route" -- proves the manual brace-depth
    // scan handles arbitrary nesting, not just the one level a prior regex
    // fix happened to tolerate.
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","plan_rwy":"20"},)"
        R"("destination":{"icao_code":"LEMD","plan_rwy":"32R"},)"
        R"("general":{"nested":{"deeper":{"a":"b"},"list":[{"x":1},{"y":2}]},)"
        R"("route":"SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL/20 SENIA2J SENIA Z596 NEXAS N975 ADUXO ADUXO3D LEMD/32R");
}

TEST_CASE("ParseSimbriefOfp: a brace character inside a quoted string doesn't confuse the object scan",
          "[SimbriefOfp]")
{
    const std::string json =
        R"({"origin":{"icao_code":"LEBL","name":"literal brace } in a string \" with an escaped quote too"},)"
        R"("destination":{"icao_code":"LEMD"},)"
        R"("general":{"route":"SENIA2J SENIA Z596"}})";
    const auto result = ParseSimbriefOfp(json);
    REQUIRE(result.origin_icao.has_value());
    CHECK(*result.origin_icao == "LEBL");
    REQUIRE(result.route_text.has_value());
    CHECK(*result.route_text == "LEBL SENIA2J SENIA Z596 LEMD");
}

TEST_CASE("ParseSimbriefFuelPlan: well-formed JSON extracts every figure and the unit", "[SimbriefOfp]")
{
    const std::string json =
        R"({"params":{"units":"kgs"},)"
        R"("fuel":{"taxi":"200","enroute_burn":"12345","contingency":"617","alternate_burn":"1500",)"
        R"("reserve":"1200","extra":"0","plan_ramp":"15862"}})";
    const auto result = ParseSimbriefFuelPlan(json);
    REQUIRE(result.units.has_value());
    CHECK(*result.units == "kgs");
    REQUIRE(result.taxi.has_value());
    CHECK(*result.taxi == 200);
    REQUIRE(result.trip.has_value());
    CHECK(*result.trip == 12345);
    REQUIRE(result.contingency.has_value());
    CHECK(*result.contingency == 617);
    REQUIRE(result.alternate.has_value());
    CHECK(*result.alternate == 1500);
    REQUIRE(result.reserve.has_value());
    CHECK(*result.reserve == 1200);
    REQUIRE(result.extra.has_value());
    CHECK(*result.extra == 0);
    REQUIRE(result.block.has_value());
    CHECK(*result.block == 15862);
}

TEST_CASE("ParseSimbriefFuelPlan: missing fuel object yields every figure nullopt", "[SimbriefOfp]")
{
    const std::string json = R"({"params":{"units":"lbs"}})";
    const auto result = ParseSimbriefFuelPlan(json);
    REQUIRE(result.units.has_value());
    CHECK(*result.units == "lbs");
    CHECK_FALSE(result.taxi.has_value());
    CHECK_FALSE(result.trip.has_value());
    CHECK_FALSE(result.block.has_value());
}

TEST_CASE("ParseSimbriefFuelPlan: missing params object leaves units nullopt but fuel figures still parse",
          "[SimbriefOfp]")
{
    const std::string json = R"({"fuel":{"taxi":"200","plan_ramp":"15862"}})";
    const auto result = ParseSimbriefFuelPlan(json);
    CHECK_FALSE(result.units.has_value());
    REQUIRE(result.taxi.has_value());
    CHECK(*result.taxi == 200);
    REQUIRE(result.block.has_value());
    CHECK(*result.block == 15862);
}

TEST_CASE("ParseSimbriefFuelPlan: malformed/non-numeric field yields nullopt for that field only", "[SimbriefOfp]")
{
    const std::string json = R"({"fuel":{"taxi":"N/A","plan_ramp":"15862"}})";
    const auto result = ParseSimbriefFuelPlan(json);
    CHECK_FALSE(result.taxi.has_value());
    REQUIRE(result.block.has_value());
    CHECK(*result.block == 15862);
}

TEST_CASE("ParseSimbriefFuelPlan: empty-string field is treated the same as missing", "[SimbriefOfp]")
{
    const std::string json = R"({"fuel":{"taxi":"","plan_ramp":"15862"}})";
    const auto result = ParseSimbriefFuelPlan(json);
    CHECK_FALSE(result.taxi.has_value());
    REQUIRE(result.block.has_value());
    CHECK(*result.block == 15862);
}

TEST_CASE("ParseSimbriefFuelPlan: reordered keys within fuel still parse", "[SimbriefOfp]")
{
    const std::string json =
        R"({"fuel":{"plan_ramp":"15862","taxi":"200","reserve":"1200"}})";
    const auto result = ParseSimbriefFuelPlan(json);
    REQUIRE(result.taxi.has_value());
    CHECK(*result.taxi == 200);
    REQUIRE(result.reserve.has_value());
    CHECK(*result.reserve == 1200);
    REQUIRE(result.block.has_value());
    CHECK(*result.block == 15862);
}

TEST_CASE("ParseSimbriefFuelPlan: malformed/truncated JSON yields every figure nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefFuelPlan("not json at all");
    CHECK_FALSE(result.units.has_value());
    CHECK_FALSE(result.taxi.has_value());
    CHECK_FALSE(result.block.has_value());
}

TEST_CASE("ParseSimbriefFuelPlan: max_tanks parses alongside the rest", "[SimbriefOfp]")
{
    const std::string json = R"({"fuel":{"plan_ramp":"15862","max_tanks":"26020"}})";
    const auto result = ParseSimbriefFuelPlan(json);
    REQUIRE(result.block.has_value());
    CHECK(*result.block == 15862);
    REQUIRE(result.max_tanks.has_value());
    CHECK(*result.max_tanks == 26020);
}

TEST_CASE("ParseSimbriefWeights: well-formed JSON extracts every figure and the unit", "[SimbriefOfp]")
{
    const std::string json =
        R"({"params":{"units":"kgs"},)"
        R"("weights":{"pax_count":"349","cargo":"10100","payload":"45200","est_zfw":"219300","max_zfw":"237700",)"
        R"("est_tow":"274000","max_tow":"294700","est_ldw":"230600","max_ldw":"251300"}})";
    const auto result = ParseSimbriefWeights(json);
    REQUIRE(result.units.has_value());
    CHECK(*result.units == "kgs");
    REQUIRE(result.pax_count.has_value());
    CHECK(*result.pax_count == 349);
    REQUIRE(result.cargo.has_value());
    CHECK(*result.cargo == 10100);
    REQUIRE(result.payload.has_value());
    CHECK(*result.payload == 45200);
    REQUIRE(result.zfw_est.has_value());
    CHECK(*result.zfw_est == 219300);
    REQUIRE(result.zfw_max.has_value());
    CHECK(*result.zfw_max == 237700);
    REQUIRE(result.tow_est.has_value());
    CHECK(*result.tow_est == 274000);
    REQUIRE(result.tow_max.has_value());
    CHECK(*result.tow_max == 294700);
    REQUIRE(result.law_est.has_value());
    CHECK(*result.law_est == 230600);
    REQUIRE(result.law_max.has_value());
    CHECK(*result.law_max == 251300);
}

TEST_CASE("ParseSimbriefWeights: missing weights object yields every figure nullopt", "[SimbriefOfp]")
{
    const std::string json = R"({"params":{"units":"lbs"}})";
    const auto result = ParseSimbriefWeights(json);
    REQUIRE(result.units.has_value());
    CHECK(*result.units == "lbs");
    CHECK_FALSE(result.pax_count.has_value());
    CHECK_FALSE(result.zfw_est.has_value());
}

TEST_CASE("ParseSimbriefWeights: reordered keys within weights still parse", "[SimbriefOfp]")
{
    const std::string json = R"({"weights":{"max_ldw":"251300","pax_count":"349","est_zfw":"219300"}})";
    const auto result = ParseSimbriefWeights(json);
    REQUIRE(result.pax_count.has_value());
    CHECK(*result.pax_count == 349);
    REQUIRE(result.zfw_est.has_value());
    CHECK(*result.zfw_est == 219300);
    REQUIRE(result.law_max.has_value());
    CHECK(*result.law_max == 251300);
}

TEST_CASE("ParseSimbriefWeights: malformed/truncated JSON yields every figure nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefWeights("not json at all");
    CHECK_FALSE(result.units.has_value());
    CHECK_FALSE(result.pax_count.has_value());
    CHECK_FALSE(result.tow_est.has_value());
}

TEST_CASE("FormatSimbriefWeightTonnes: kgs formats as tonnes to one decimal", "[SimbriefOfp]")
{
    CHECK(FormatSimbriefWeightTonnes(219300, std::optional<std::string>("kgs")) == "219.3");
    CHECK(FormatSimbriefWeightTonnes(349, std::optional<std::string>("kgs")) == "0.3");
}

TEST_CASE("FormatSimbriefWeightTonnes: lbs (or missing units) formats as the raw whole-unit figure", "[SimbriefOfp]")
{
    CHECK(FormatSimbriefWeightTonnes(483000, std::optional<std::string>("lbs")) == "483000");
    CHECK(FormatSimbriefWeightTonnes(349, std::nullopt) == "349");
}

TEST_CASE("ParseSimbriefHeader: well-formed JSON (real Simbrief field shapes) extracts every figure",
          "[SimbriefOfp]")
{
    // Field names and epoch values lifted from a real Simbrief OFP response
    // (LEBL-LEMD, captured via the SIMBRIEFDBG debug log) -- 1784383200 is
    // 18JUL2026 14:00 UTC, 1784381252 is 18JUL2026 13:27:32 UTC.
    const std::string json =
        R"({"params":{"time_generated":"1784381252"},)"
        R"("general":{"icao_airline":"IB","flight_number":"2301","costindex":"6","release":"1",)"
        R"("stepclimb_string":"LEBL\/0290","avg_wind_comp":"-38","avg_temp_dev":"10"},)"
        R"("aircraft":{"icaocode":"A319","reg":"C-GTLS"},)"
        R"("alternate":{"icao_code":"LEZG"},)"
        R"("times":{"sched_out":"1784383200"}})";
    const auto result = ParseSimbriefHeader(json);
    REQUIRE(result.callsign.has_value());
    CHECK(*result.callsign == "IB2301");
    REQUIRE(result.aircraft_type.has_value());
    CHECK(*result.aircraft_type == "A319");
    REQUIRE(result.aircraft_reg.has_value());
    CHECK(*result.aircraft_reg == "C-GTLS");
    REQUIRE(result.cost_index.has_value());
    CHECK(*result.cost_index == "6");
    REQUIRE(result.departure_date.has_value());
    CHECK(*result.departure_date == "18JUL2026");
    REQUIRE(result.release_id.has_value());
    CHECK(*result.release_id == "1");
    REQUIRE(result.release_date.has_value());
    CHECK(*result.release_date == "18JUL26");
    REQUIRE(result.alternate_icao.has_value());
    CHECK(*result.alternate_icao == "LEZG");
    REQUIRE(result.step_climbs.has_value());
    CHECK(*result.step_climbs == "LEBL/0290");
    REQUIRE(result.avg_wind_component.has_value());
    CHECK(*result.avg_wind_component == "M038");
    REQUIRE(result.avg_isa_deviation.has_value());
    CHECK(*result.avg_isa_deviation == "P010");
}

TEST_CASE("ParseSimbriefHeader: missing objects yield every figure nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefHeader("{}");
    CHECK_FALSE(result.callsign.has_value());
    CHECK_FALSE(result.aircraft_type.has_value());
    CHECK_FALSE(result.alternate_icao.has_value());
    CHECK_FALSE(result.departure_date.has_value());
}

TEST_CASE("ParseSimbriefHeader: callsign is nullopt if either airline or flight number is missing",
          "[SimbriefOfp]")
{
    const std::string json = R"({"general":{"icao_airline":"IB"}})";
    const auto result = ParseSimbriefHeader(json);
    CHECK_FALSE(result.callsign.has_value());
}

TEST_CASE("ParseSimbriefHeader: malformed/truncated JSON yields every figure nullopt", "[SimbriefOfp]")
{
    const auto result = ParseSimbriefHeader("not json at all");
    CHECK_FALSE(result.callsign.has_value());
    CHECK_FALSE(result.departure_date.has_value());
    CHECK_FALSE(result.avg_wind_component.has_value());
}
