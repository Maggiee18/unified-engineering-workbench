#include "workbench/core/errors.hpp"
#include "workbench/thermal/nodal_csv.hpp"

#include <doctest.h>

#include "test_data_dir.hpp"

#include <string>

using namespace wb;
using namespace wb::thermal;

namespace {
const std::string kHeader = "node_id,x_m,y_m,z_m,temperature_C\n";

void expectParseError(const std::string& text, const std::string& fragment) {
    try {
        parseNodalTemperatureCsv(text);
        FAIL("expected ParseError containing: " << fragment);
    } catch (const ParseError& e) {
        CHECK_MESSAGE(std::string(e.what()).find(fragment) != std::string::npos, std::string(e.what()));
    }
}
}  // namespace

TEST_CASE("CSV positive: valid rows, ranges, comments and BOM") {
    const std::string text = "\xEF\xBB\xBF# SYNTHETIC test data\n" + kHeader +
                             "1,0,0,0,20.5\n2,0.1,0,0,35\n\n3,0.2,0.05,-0.01,99.25\n";
    const NodalDataset d = parseNodalTemperatureCsv(text);
    REQUIRE(d.nodes.size() == 3);
    CHECK(d.minTemperatureC == doctest::Approx(20.5));
    CHECK(d.maxTemperatureC == doctest::Approx(99.25));
    CHECK(d.minCoord[2] == doctest::Approx(-0.01));
    REQUIRE(d.comments.size() == 1);
    CHECK(d.comments[0] == "SYNTHETIC test data");
}

TEST_CASE("CSV positive: column order may differ, quotes and CRLF tolerated, extras ignored") {
    const std::string text = "temperature_C,node_id,z_m,y_m,x_m,label\r\n\"25\",7,0,0,1.5,a\r\n";
    const NodalDataset d = parseNodalTemperatureCsv(text);
    REQUIRE(d.nodes.size() == 1);
    CHECK(d.nodes[0].nodeId == 7);
    CHECK(d.nodes[0].x == doctest::Approx(1.5));
    CHECK(d.nodes[0].temperatureC == doctest::Approx(25.0));
    CHECK(d.warnings.size() == 1);
}

TEST_CASE("CSV negative: empty, whitespace and header-only files") {
    expectParseError("", "empty");
    expectParseError("\n  \n", "only whitespace");
    expectParseError(kHeader, "no data rows");
    expectParseError("# just a comment\n", "no header row");
}

TEST_CASE("CSV negative: missing or duplicate columns") {
    expectParseError("node_id,x_m,y_m,temperature_C\n1,0,0,5\n", "missing required column(s): z_m");
    expectParseError("node_id,x_m,y_m,z_m,temperature_C,x_m\n", "duplicate header column");
    expectParseError("id,x,y,z,t\n1,2,3,4,5\n", "node_id, x_m, y_m, z_m, temperature_C");
}

TEST_CASE("CSV negative: field count, non-numeric and non-finite values") {
    expectParseError(kHeader + "1,0,0,0\n", "expected 5 fields, found 4");
    expectParseError(kHeader + "1,0,zero,0,20\n", "'y_m' value 'zero' is not a number");
    expectParseError(kHeader + "1,0,0,0,NaN\n", "not finite");
    expectParseError(kHeader + "1,inf,0,0,20\n", "not finite");
    expectParseError(kHeader + "1,0,0,0,20\n2,0,0,0,-1e400\n", "line 3");  // overflow -> -inf
    expectParseError(kHeader + "1.5,0,0,0,20\n", "not an integer");
    expectParseError(kHeader + "0,0,0,0,20\n", "positive integer");
}

TEST_CASE("CSV negative: physical and integrity checks") {
    expectParseError(kHeader + "1,0,0,0,-300\n", "below absolute zero");
    expectParseError(kHeader + "1,0,0,0,20\n1,1,0,0,21\n", "duplicate node_id 1");
}

TEST_CASE("CSV negative: limits") {
    NodalCsvLimits limits;
    limits.maxNodes = 1;
    CHECK_THROWS_AS(parseNodalTemperatureCsv(kHeader + "1,0,0,0,1\n2,0,0,0,1\n", limits), ParseError);
}

TEST_CASE("CSV example data file is labelled synthetic and loads") {
    const NodalDataset d =
        loadNodalTemperatureCsvFile(testDataDir() + "/thermal/synthetic_nodal_temperatures.csv");
    CHECK(d.nodes.size() > 100);
    REQUIRE_FALSE(d.comments.empty());
    CHECK(d.comments[0].find("SYNTHETIC") != std::string::npos);
    CHECK(d.maxTemperatureC > d.minTemperatureC);
}
