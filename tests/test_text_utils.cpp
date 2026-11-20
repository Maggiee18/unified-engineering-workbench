#include "workbench/utilities/text.hpp"

#include <doctest.h>

#include <cmath>

using namespace wb::util;

TEST_CASE("parseDouble accepts C-locale numbers and rejects junk") {
    CHECK(parseDouble("1.5").value() == doctest::Approx(1.5));
    CHECK(parseDouble("+2e-3").value() == doctest::Approx(0.002));
    CHECK(parseDouble("-0.25E+01").value() == doctest::Approx(-2.5));
    CHECK_FALSE(parseDouble("").has_value());
    CHECK_FALSE(parseDouble("1,5").has_value());   // comma decimal must not be accepted silently
    CHECK_FALSE(parseDouble("12abc").has_value());
    CHECK_FALSE(parseDouble("abc").has_value());
    // nan/inf parse but are flagged non-finite by callers
    CHECK(std::isnan(parseDouble("nan").value()));
    CHECK(std::isinf(parseDouble("inf").value()));
}

TEST_CASE("parseInt64 is strict") {
    CHECK(parseInt64("42").value() == 42);
    CHECK_FALSE(parseInt64("4.2").has_value());
    CHECK_FALSE(parseInt64("").has_value());
}

TEST_CASE("splitting and trimming") {
    CHECK(trim("  a b \r\n") == "a b");
    const auto ws = splitWhitespace("  vertex  1 2\t3 ");
    REQUIRE(ws.size() == 4);
    CHECK(ws[0] == "vertex");
    CHECK(ws[3] == "3");
    const auto f = splitDelimited("a,,b", ',');
    REQUIRE(f.size() == 3);
    CHECK(f[1].empty());
}

TEST_CASE("forEachLine handles CRLF and missing final newline") {
    std::vector<std::string> lines;
    forEachLine("a\r\nb\nc", [&](std::size_t, std::string_view l) { lines.emplace_back(l); });
    REQUIRE(lines.size() == 3);
    CHECK(lines[0] == "a");
    CHECK(lines[2] == "c");
}
