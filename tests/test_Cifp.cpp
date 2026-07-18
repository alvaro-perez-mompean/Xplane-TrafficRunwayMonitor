#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <sstream>

#include "core/Cifp.h"

using namespace trm::core;

namespace {

bool Contains(const std::vector<std::string>& idents, const std::string& ident)
{
    return std::find(idents.begin(), idents.end(), ident) != idents.end();
}

} // namespace

TEST_CASE("ParseCifp groups consecutive same-ident/same-transition SID lines into one procedure", "[Cifp]")
{
    // Real-shape LEBL fixture: SENI2J/RW20 and SENI3K/RW20 both terminate at
    // SENIA; REBU1S/RW02 is an unrelated SID/runway, included to confirm it
    // doesn't bleed into the SENIA matches.
    std::istringstream in(
        "SID:005,5,SENI2J,RW20,DER20,LE,P,C,EY  , ,   ,DF, , , , , ,      ,    ,    ,    ,    , ,     ,     ,06000, ,   ,    ,   , , , , , , , , ;\n"
        "SID:010,5,SENI2J,RW20, , , , ,    , ,   ,CA, , , , , ,      ,    ,    ,1980,    ,+,00500,     ,     , ,   ,    ,   , , , , , , , , ;\n"
        "SID:070,5,SENI2J,RW20,SENIA,LE,E,A,EEC , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n"
        "SID:090,5,SENI3K,RW20,SENIA,LE,E,A,EEC , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n"
        "SID:070,5,REBU1S,RW02,REBUL,LE,E,A,EEC , ,   ,TF, , , , , ,      ,    ,    ,    ,    , ,     ,     ,     , ,   ,    ,   , , , , , , , , ;\n");

    const CifpProcedures procs = ParseCifp(in);
    REQUIRE(procs.sids.size() == 3);

    const CifpProcedure& seni2j = procs.sids[0];
    CHECK(seni2j.ident == "SENI2J");
    CHECK(seni2j.runway_transition == "RW20");
    REQUIRE(seni2j.legs.size() == 3);
    CHECK(seni2j.legs.back().waypoint_id == "SENIA");

    const std::vector<std::string> matches = FindSidsForRunwayFix(procs.sids, "20", "SENIA");
    CHECK(Contains(matches, "SENI2J"));
    CHECK(Contains(matches, "SENI3K"));
    CHECK_FALSE(Contains(matches, "REBU1S"));
    CHECK(matches.size() == 2);
}

TEST_CASE("FindSidsForRunwayFix requires both the runway and the terminal fix to match", "[Cifp]")
{
    std::istringstream in(
        "SID:070,5,SENI2J,RW20,SENIA,LE,E,A,EEC , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n"
        "SID:060,5,SENI2S,RW02,SENIA,LE,E,A,EEC , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n");
    const CifpProcedures procs = ParseCifp(in);

    CHECK(FindSidsForRunwayFix(procs.sids, "20", "SENIA") == std::vector<std::string>{"SENI2J"});
    CHECK(FindSidsForRunwayFix(procs.sids, "02", "SENIA") == std::vector<std::string>{"SENI2S"});
    CHECK(FindSidsForRunwayFix(procs.sids, "20", "NOTFOUND").empty());
    CHECK(FindSidsForRunwayFix(procs.sids, "06", "SENIA").empty());
}

TEST_CASE("RunwayTransitionCovers handles the both-ends 'B' convention and exact matches", "[Cifp]")
{
    CHECK(RunwayTransitionCovers("RW20", "20"));
    CHECK_FALSE(RunwayTransitionCovers("RW20", "02"));
    CHECK(RunwayTransitionCovers("RW06B", "06L"));
    CHECK(RunwayTransitionCovers("RW06B", "06R"));
    CHECK_FALSE(RunwayTransitionCovers("RW06B", "06C"));
    CHECK_FALSE(RunwayTransitionCovers("", "20"));
    CHECK_FALSE(RunwayTransitionCovers("REBUL", "20")); // named enroute transition, not a runway one
}

TEST_CASE("FindStarsForFix matches on the STAR's first leg (its named entry fix), not runway", "[Cifp]")
{
    // Real-shape LEMD fixture: ADUX3D/RW32B enters at ADUXO; ADUX7B/RW18B is
    // a different runway group entering at the same fix, and must still
    // match since STAR selection here is runway-independent.
    std::istringstream in(
        "STAR:010,5,ADUX3D,RW32B,ADUXO,LE,E,A,E  H, ,   ,IF, , , , , ,      ,    ,    ,    ,    ,-,FL210,     ,     , ,   ,    ,   , , , , , , , , ;\n"
        "STAR:020,5,ADUX3D,RW32B,MD001,LE,E,A,E   , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n"
        "STAR:010,5,ADUX7B,RW18B,ADUXO,LE,E,A,E  H, ,   ,IF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n"
        "STAR:010,5,BANE3B,RW18B,BANEV,LE,E,A,E   , ,   ,IF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n");
    const CifpProcedures procs = ParseCifp(in);

    const std::vector<std::string> matches = FindStarsForFix(procs.stars, "ADUXO");
    CHECK(Contains(matches, "ADUX3D"));
    CHECK(Contains(matches, "ADUX7B"));
    CHECK_FALSE(Contains(matches, "BANE3B"));
    CHECK(matches.size() == 2);

    CHECK(FindStarsForFix(procs.stars, "MD001").empty()); // mid-route fix, not the entry -- shouldn't match
    CHECK(FindStarsForFix(procs.stars, "").empty());
}

TEST_CASE("FindApproachesForRunway matches idents encoding the runway and prefers ILS first", "[Cifp]")
{
    std::istringstream in(
        "APPCH:010,D,D32L, ,CD32L,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n"
        "APPCH:010,D,I32LZ, ,CI32LZ,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n"
        "APPCH:010,D,I32LW, ,CI32LW,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n"
        "APPCH:010,D,R32LY, ,CR32LY,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n"
        "APPCH:010,D,I18RY, ,CI18RY,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n");
    const CifpProcedures procs = ParseCifp(in);

    const std::vector<std::string> matches = FindApproachesForRunway(procs.approaches, "32L");
    REQUIRE(matches.size() == 4);
    CHECK(matches[0][0] == 'I'); // an ILS variant sorts first
    CHECK(matches[1][0] == 'I');
    CHECK(Contains(matches, "D32L"));
    CHECK(Contains(matches, "R32LY"));
    CHECK_FALSE(Contains(matches, "I18RY"));

    CHECK(FindApproachesForRunway(procs.approaches, "14R").empty());
}

TEST_CASE("FindApproachesForRunway handles the no-side hyphenated suffix shape", "[Cifp]")
{
    // Real-shape LEBL fixture: runway "02" has no L/R/C side, so a suffix
    // letter is separated with a literal '-' in the raw ident ("I02-Y")
    // rather than appended directly the way a parallel-runway ident does
    // ("I06LY").
    std::istringstream in(
        "APPCH:010,D,I02-Y, ,CI02Y,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n"
        "APPCH:010,D,I02-Z, ,CI02Z,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n"
        "APPCH:010,D,D02, ,CD02,LE,P,C,E  I, ,   ,IF, ,BRA,LE,D, ,      ,3596,0185,    ,    ,+,06300,     ,     , ,   ,    ,   , , , , , ,0, ,S;\n");
    const CifpProcedures procs = ParseCifp(in);

    const std::vector<std::string> matches = FindApproachesForRunway(procs.approaches, "02");
    CHECK(Contains(matches, "I02-Y"));
    CHECK(Contains(matches, "I02-Z"));
    CHECK(Contains(matches, "D02"));
    CHECK(matches.size() == 3);
}

TEST_CASE("FormatApproachIdentForDisplay spells out the type and hyphenates the suffix", "[Cifp]")
{
    CHECK(FormatApproachIdentForDisplay("I32RW") == "ILS32R-W");
    CHECK(FormatApproachIdentForDisplay("D18L") == "VOR/DME18L");
    CHECK(FormatApproachIdentForDisplay("L06L") == "LOC06L");
    CHECK(FormatApproachIdentForDisplay("R24RY") == "RNAV24R-Y");
    CHECK(FormatApproachIdentForDisplay("I02-Y") == "ILS02-Y");
    CHECK(FormatApproachIdentForDisplay("D02") == "VOR/DME02");
}

TEST_CASE("FormatApproachIdentForDisplay falls back to the bare ident for an unrecognized shape", "[Cifp]")
{
    CHECK(FormatApproachIdentForDisplay("") == "");
    CHECK(FormatApproachIdentForDisplay("XY") == "XY");
    CHECK(FormatApproachIdentForDisplay("NOTANIDENT") == "NOTANIDENT");
}

TEST_CASE("ExtractDepartureAnchorFix reads the second token (SID name, then fix)", "[Cifp]")
{
    std::istringstream in(
        "SID:070,5,SENI2J,RW20,SENIA,LE,E,A,EEC , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n");
    const CifpProcedures procs = ParseCifp(in);

    const auto fix = ExtractDepartureAnchorFix("SENIA2J SENIA Z596 NEXAS", procs.sids, "20");
    REQUIRE(fix.has_value());
    CHECK(*fix == "SENIA");
}

TEST_CASE("ExtractDepartureAnchorFix falls back to the first token when no SID is assigned", "[Cifp]")
{
    // No SID for RW20 reaches "SENIA2J" (it's not a real fix in this
    // fixture) -- but "DIRECTFIX" is, at the front of the route with
    // nothing before it, i.e. no SID assigned at all.
    std::istringstream in(
        "SID:070,5,SENI2J,RW20,DIRECTFIX,LE,E,A,EEC , ,   ,TF, , , , , ,      ,    ,    ,    ,    ,+,FL150,     ,     , ,   ,    ,   , , , , , , , , ;\n");
    const CifpProcedures procs = ParseCifp(in);

    const auto fix = ExtractDepartureAnchorFix("DIRECTFIX Z596 NEXAS", procs.sids, "20");
    REQUIRE(fix.has_value());
    CHECK(*fix == "DIRECTFIX");
}

TEST_CASE("ExtractArrivalAnchorFix reads the second-to-last token (fix, then STAR name)", "[Cifp]")
{
    std::istringstream in(
        "STAR:010,5,ADUX3D,RW32B,ADUXO,LE,E,A,E  H, ,   ,IF, , , , , ,      ,    ,    ,    ,    ,-,FL210,     ,     , ,   ,    ,   , , , , , , , , ;\n");
    const CifpProcedures procs = ParseCifp(in);

    const auto fix = ExtractArrivalAnchorFix("Z596 NEXAS N975 ADUXO ADUXO3D", procs.stars);
    REQUIRE(fix.has_value());
    CHECK(*fix == "ADUXO");
}

TEST_CASE("ExtractArrivalAnchorFix falls back to the last token when no STAR is assigned", "[Cifp]")
{
    std::istringstream in(
        "STAR:010,5,ADUX3D,RW32B,DIRECTFIX,LE,E,A,E  H, ,   ,IF, , , , , ,      ,    ,    ,    ,    ,-,FL210,     ,     , ,   ,    ,   , , , , , , , , ;\n");
    const CifpProcedures procs = ParseCifp(in);

    const auto fix = ExtractArrivalAnchorFix("Z596 NEXAS DIRECTFIX", procs.stars);
    REQUIRE(fix.has_value());
    CHECK(*fix == "DIRECTFIX");
}

TEST_CASE("ParseCifp ignores unrelated record types and malformed lines", "[Cifp]")
{
    std::istringstream in(
        "RWY:20,090100,+038071924,-000472842,00000053,0000,        ,0.0,       ,0,0,\n"
        "PRDAT:010,SENI2J,,,,,,,,,,,,,,,\n"
        "SID:\n"
        "SID:005,5\n");
    const CifpProcedures procs = ParseCifp(in);
    CHECK(procs.sids.empty());
    CHECK(procs.stars.empty());
    CHECK(procs.approaches.empty());
}

TEST_CASE("FormatProcedureSummary omits missing pieces", "[Cifp]")
{
    CHECK(FormatProcedureSummary("LEBL", "20", "SENI2J", std::nullopt, "LEMD", "32R", "I32LZ") ==
          "LEBL/20 SENI2J LEMD/32R I32LZ");
    CHECK(FormatProcedureSummary("LEBL", std::nullopt, std::nullopt, std::nullopt, "LEMD", std::nullopt,
                                  std::nullopt) == "LEBL LEMD");
    CHECK(FormatProcedureSummary("LEBL", "20", "SENI2J", "ADUX3D", "LEMD", std::nullopt, std::nullopt) ==
          "LEBL/20 SENI2J  ADUX3D LEMD");
}
