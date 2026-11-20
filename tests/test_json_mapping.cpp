// Transport-layer input validation (JSON request bodies -> core types).
#include "server/json_mapping.hpp"

#include "workbench/core/errors.hpp"

#include <doctest.h>

using namespace wb;
using wb::api::Json;

TEST_CASE("Rod problem from JSON: defaults, overrides and rejection") {
    const auto p = api::rodProblemFromJson(Json::object());
    CHECK(p.conductivity == 10.0);
    CHECK(api::rodProblemFromJson(Json{{"k", 20}}).conductivity == 20.0);
    CHECK_THROWS_AS(api::rodProblemFromJson(Json{{"k", "ten"}}), ValidationError);
    CHECK_THROWS_AS(api::rodProblemFromJson(Json{{"k", -1}}), ValidationError);
}

TEST_CASE("Benchmark config from JSON: invalid dimensions and worker counts") {
    CHECK_NOTHROW(api::benchmarkConfigFromJson(Json{{"cases", 10}, {"elements", 10}, {"workers", {1, 2}}}));
    CHECK_THROWS_AS(api::benchmarkConfigFromJson(Json{{"cases", -5}}), ValidationError);
    CHECK_THROWS_AS(api::benchmarkConfigFromJson(Json{{"cases", 2.5}}), ValidationError);
    CHECK_THROWS_AS(api::benchmarkConfigFromJson(Json{{"workers", {0}}}), ValidationError);
    CHECK_THROWS_AS(api::benchmarkConfigFromJson(Json{{"workers", "four"}}), ValidationError);
    CHECK_THROWS_AS(api::benchmarkConfigFromJson(Json{{"elements", 0}}), ValidationError);
    CHECK_THROWS_AS(api::benchmarkConfigFromJson(Json{{"repetitions", 0}}), ValidationError);
}

TEST_CASE("Solution JSON carries units-consistent summary values") {
    const auto s = thermal::solveRod(thermal::RodProblem{}, 10);
    const auto j = api::toJson(s, thermal::evaluateMidpointError(s));
    CHECK(j["elements"] == 10);
    CHECK(j["x"].size() == 11);
    CHECK(j["midpointX"].size() == 10);
    CHECK(j["maxMidpointError"].get<double>() == doctest::Approx(1.25));
    CHECK(j["heatGeneratedW"].get<double>() == doctest::Approx(1.0));
}
