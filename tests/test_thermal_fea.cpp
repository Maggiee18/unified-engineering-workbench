#include "workbench/core/errors.hpp"
#include "workbench/core/linear_algebra.hpp"
#include "workbench/thermal/rod_fea.hpp"

#include <doctest.h>

#include <cmath>

using namespace wb;
using namespace wb::thermal;

TEST_CASE("FEA: exact solution reproduces the assignment formula") {
    const RodProblem p;  // assignment defaults
    for (double x : {0.0, 0.1, 0.37, 0.5, 1.0}) {
        const double assignment = 100.0 - 80.0 * x / 1.0 + (10000.0 / (2.0 * 10.0)) * x * (1.0 - x);
        CHECK(exactTemperature(p, x) == doctest::Approx(assignment).epsilon(1e-14));
    }
    CHECK(exactTemperature(p, 0.0) == 100.0);
    CHECK(exactTemperature(p, 1.0) == doctest::Approx(20.0));
}

TEST_CASE("FEA: element assembly matches hand-computed 2-element system") {
    // N = 2, h = 0.5: kA/h = 10*1e-4/0.5 = 2e-3 W/K; qAh/2 = 1e4*1e-4*0.5/2 = 0.25 W
    const RodProblem p;
    la::DenseMatrix K(3);
    std::vector<double> F;
    assembleRodSystem(p, 2, K, F);
    CHECK(K.at(0, 0) == doctest::Approx(2e-3));
    CHECK(K.at(1, 1) == doctest::Approx(4e-3));
    CHECK(K.at(0, 1) == doctest::Approx(-2e-3));
    CHECK(K.at(0, 2) == 0.0);
    CHECK(F[0] == doctest::Approx(0.25));
    CHECK(F[1] == doctest::Approx(0.5));
    CHECK(F[2] == doctest::Approx(0.25));
    // Each row of an unconstrained conduction matrix sums to zero (rigid-body mode).
    for (std::size_t i = 0; i < 3; ++i) CHECK(K.at(i, 0) + K.at(i, 1) + K.at(i, 2) == doctest::Approx(0.0));
}

TEST_CASE("FEA: tridiagonal path agrees with an independent dense Gaussian elimination") {
    const RodProblem p;
    for (std::size_t n : {1u, 3u, 10u, 37u}) {
        la::DenseMatrix Kd(n + 1);
        std::vector<double> Fd;
        assembleRodSystem(p, n, Kd, Fd);
        applyPrescribedTemperature(Kd, Fd, 0, p.temperatureLeft);
        applyPrescribedTemperature(Kd, Fd, n, p.temperatureRight);
        const std::vector<double> Td = Kd.solve(Fd);
        const RodSolution s = solveRod(p, n);
        for (std::size_t i = 0; i <= n; ++i) CHECK(s.temperature[i] == doctest::Approx(Td[i]).epsilon(1e-12));
    }
}

TEST_CASE("FEA: boundary conditions are imposed exactly and K stays symmetric") {
    const RodProblem p;
    la::DenseMatrix K(11);
    std::vector<double> F;
    assembleRodSystem(p, 10, K, F);
    applyPrescribedTemperature(K, F, 0, 100.0);
    applyPrescribedTemperature(K, F, 10, 20.0);
    for (std::size_t i = 0; i < 11; ++i)
        for (std::size_t j = 0; j < 11; ++j) CHECK(K.at(i, j) == doctest::Approx(K.at(j, i)));
    const RodSolution s = solveRod(p, 10);
    CHECK(s.temperature.front() == 100.0);
    CHECK(s.temperature.back() == 20.0);
}

TEST_CASE("FEA: nodal values are exact for 1D linear elements (superconvergence)") {
    const RodSolution s = solveRod(RodProblem{}, 10);
    const MidpointErrorReport r = evaluateMidpointError(s);
    CHECK(r.maxNodalError < 1e-9);
}

TEST_CASE("FEA: midpoint error from FE interpolation equals q h^2 / (8k)") {
    // Since nodal values are exact and T'' = -q/k, the linear interpolant's
    // midpoint error is (h^2/8)(q/k) at *every* element: 1.25 C for N = 10.
    const double expected[] = {1.25, 0.3125, 0.078125, 0.01953125};
    const std::size_t meshes[] = {10, 20, 40, 80};
    for (int i = 0; i < 4; ++i) {
        const MidpointErrorReport r = evaluateMidpointError(solveRod(RodProblem{}, meshes[i]));
        CHECK(r.maxMidpointError == doctest::Approx(expected[i]).epsilon(1e-9));
        CHECK(r.predictedMidpointError == doctest::Approx(expected[i]));
        for (std::size_t e = 0; e < r.midpointX.size(); ++e) {
            CHECK(r.midpointExact[e] - r.midpointFe[e] == doctest::Approx(expected[i]).epsilon(1e-8));
        }
    }
}

TEST_CASE("FEA: mesh convergence is second order") {
    const auto rows = runConvergenceStudy(RodProblem{}, {10, 20, 40, 80});
    REQUIRE(rows.size() == 4);
    CHECK(rows[0].observedOrder == 0.0);
    for (std::size_t i = 1; i < rows.size(); ++i) CHECK(rows[i].observedOrder == doctest::Approx(2.0).epsilon(1e-6));
}

TEST_CASE("FEA: energy balance - boundary heat flow equals generated heat") {
    const RodProblem p;
    const RodSolution s = solveRod(p, 40);
    const double generated = p.heatGeneration * p.area * p.length;  // 1 W
    // Reactions are heat *into* the rod; at steady state they sum to -generated.
    CHECK(s.reactionLeft + s.reactionRight == doctest::Approx(-generated).epsilon(1e-10));
    // Exact boundary fluxes: q_out = k A |dT/dx|; dT/dx(0) = -80 + 500 = 420 K/m -> 0.42 W leaves at x=0... sign:
    // heat into rod at x=0 is +kA dT/dx(0)*(-1)... = -kA*420 = -0.42 W; at x=L: kA*dT/dx(L) = kA*(-580) = -0.58 W.
    CHECK(s.reactionLeft == doctest::Approx(-0.42).epsilon(1e-9));
    CHECK(s.reactionRight == doctest::Approx(-0.58).epsilon(1e-9));
}

TEST_CASE("FEA: interpolation uses shape functions and rejects points outside the rod") {
    const RodSolution s = solveRod(RodProblem{}, 4);
    CHECK(s.interpolate(0.0) == doctest::Approx(100.0));
    CHECK(s.interpolate(1.0) == doctest::Approx(20.0));
    CHECK(s.interpolate(0.125) == doctest::Approx(0.5 * (s.temperature[0] + s.temperature[1])));
    CHECK_THROWS_AS(s.interpolate(1.5), ValidationError);
    CHECK_THROWS_AS(s.interpolate(-0.1), ValidationError);
}

TEST_CASE("FEA: zero heat generation gives a linear profile, exact everywhere") {
    RodProblem p;
    p.heatGeneration = 0.0;
    const MidpointErrorReport r = evaluateMidpointError(solveRod(p, 7));
    CHECK(r.maxMidpointError < 1e-10);
}

TEST_CASE("FEA negative: invalid parameters are rejected with messages") {
    RodProblem p;
    p.conductivity = 0.0;
    CHECK_THROWS_WITH_AS(solveRod(p, 10), doctest::Contains("Thermal conductivity k"), ValidationError);
    p = RodProblem{};
    p.length = -1.0;
    CHECK_THROWS_AS(solveRod(p, 10), ValidationError);
    p = RodProblem{};
    p.area = std::nan("");
    CHECK_THROWS_AS(solveRod(p, 10), ValidationError);
    p = RodProblem{};
    p.temperatureLeft = -400.0;
    CHECK_THROWS_AS(solveRod(p, 10), ValidationError);
    CHECK_THROWS_AS(solveRod(RodProblem{}, 0), ValidationError);
    CHECK_THROWS_AS(solveRod(RodProblem{}, kMaxElements + 1), ValidationError);
}

TEST_CASE("Linear algebra: tridiagonal band enforcement and singular detection") {
    la::TridiagonalMatrix T(3);
    CHECK_THROWS_AS(T.add(0, 2, 1.0), std::out_of_range);
    CHECK_THROWS_AS(T.solve({1, 2, 3}), NumericalError);  // all-zero matrix
    la::DenseMatrix D(2);
    CHECK_THROWS_AS(D.solve({1, 1}), NumericalError);
}
