#include "workbench/core/errors.hpp"
#include "workbench/geometry/stl_mesh.hpp"

#include <doctest.h>

#include "test_data_dir.hpp"

#include <string>

using namespace wb;
using namespace wb::geometry;

namespace {
const char* kOneTriangle = R"(solid tri
  facet normal 0 0 1
    outer loop
      vertex 0 0 0
      vertex 1 0 0
      vertex 0 1 0
    endloop
  endfacet
endsolid tri
)";

std::string facet(const std::string& v3) {
    return "facet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\n" + v3 + "\nendloop\nendfacet\n";
}

/// Expects a ParseError whose message contains `fragment`.
void expectParseError(const std::string& text, const std::string& fragment) {
    try {
        parseAsciiStl(text);
        FAIL("expected ParseError containing: " << fragment);
    } catch (const ParseError& e) {
        CHECK_MESSAGE(std::string(e.what()).find(fragment) != std::string::npos, std::string(e.what()));
    }
}
}  // namespace

TEST_CASE("STL positive: single triangle parses with bounds and area") {
    const StlMesh m = parseAsciiStl(kOneTriangle);
    REQUIRE(m.triangles.size() == 1);
    CHECK(m.solidName == "tri");
    CHECK(m.surfaceArea == doctest::Approx(0.5));
    CHECK(m.bounds.max.x == doctest::Approx(1.0));
    CHECK(m.bounds.min.z == doctest::Approx(0.0));
    CHECK(m.warnings.empty());
}

TEST_CASE("STL positive: keywords are case-insensitive, CRLF and scientific notation accepted") {
    const std::string text =
        "SOLID cube\r\nFACET NORMAL 0 0 -1\r\nOUTER LOOP\r\nVERTEX 0 0 0\r\nVERTEX 1.0e+00 0 0\r\n"
        "VERTEX 0 +1E0 0\r\nENDLOOP\r\nENDFACET\r\nENDSOLID cube\r\n";
    CHECK(parseAsciiStl(text).triangles.size() == 1);
}

TEST_CASE("STL positive: zero normal is recomputed and reported") {
    std::string text = kOneTriangle;
    text.replace(text.find("0 0 1"), 5, "0 0 0");
    const StlMesh m = parseAsciiStl(text);
    CHECK(m.triangles[0].normal.z == doctest::Approx(1.0));
    CHECK(m.recomputedNormals == 1);
}

TEST_CASE("STL positive: degenerate triangle is kept with a warning") {
    const StlMesh m = parseAsciiStl("solid d\n" + facet("vertex 2 0 0") + "endsolid d\n");
    CHECK(m.degenerateTriangles == 1);
    CHECK_FALSE(m.warnings.empty());
}

TEST_CASE("STL negative: empty and whitespace-only files are explained") {
    expectParseError("", "empty");
    expectParseError("   \n\t\n", "only whitespace");
}

TEST_CASE("STL negative: malformed structure") {
    expectParseError("facet normal 0 0 1\n", "must start with 'solid'");
    expectParseError("solid x\nendsolid x\n", "no facets");
    expectParseError("solid x\n" + facet("vertex 0 1 0").substr(0, 43), "unexpected end of file");
    expectParseError("solid x\nfacet normal 0 0\n", "facet normal nx ny nz");
    expectParseError("solid x\nfacet normal 0 0 1\nouter loop\nvertex 0 0 0\nvertex 1 0 0\nendloop\n", "exactly 3");
    expectParseError("solid x\n" + facet("vertex 0 1 0\nvertex 5 5 5") + "endsolid\n", "more than 3");
    expectParseError("solid x\n" + facet("vertex 0 1") + "endsolid\n", "vertex x y z");
    expectParseError(std::string(kOneTriangle) + "solid again\n", "multiple solids");
}

TEST_CASE("STL negative: non-numeric and non-finite coordinates are rejected with line numbers") {
    expectParseError("solid x\n" + facet("vertex 0 abc 0") + "endsolid\n", "not a number");
    expectParseError("solid x\n" + facet("vertex 0 nan 0") + "endsolid\n", "not finite");
    expectParseError("solid x\n" + facet("vertex inf 0 0") + "endsolid\n", "line 6");
}

TEST_CASE("STL negative: binary STL is detected") {
    std::string binary = "solid binary-header";
    binary.append(80 - binary.size(), '\0');
    binary += std::string("\x01\x00\x00\x00", 4);
    expectParseError(binary, "binary STL");
}

TEST_CASE("STL negative: limits are enforced") {
    StlLimits limits;
    limits.maxBytes = 10;
    CHECK_THROWS_AS(parseAsciiStl(kOneTriangle, limits), ParseError);
    limits = StlLimits{};
    limits.maxTriangles = 1;
    const std::string two = "solid t\n" + facet("vertex 0 1 0") + facet("vertex 0 1 0") + "endsolid\n";
    CHECK_THROWS_AS(parseAsciiStl(two, limits), ParseError);
}

TEST_CASE("STL example data file loads") {
    const StlMesh m = loadAsciiStlFile(testDataDir() + "/geometry/heatsink_bracket.stl");
    CHECK(m.triangles.size() > 100);
    CHECK(m.bounds.diagonal() > 0.0);
    CHECK(m.degenerateTriangles == 0);
}

TEST_CASE("STL missing file gives a validation error") {
    CHECK_THROWS_AS(loadAsciiStlFile("does/not/exist.stl"), ValidationError);
}
