// Steady-state 1D thermal finite-element solver for a uniform rod.
//
// Governing equation (insulated lateral surface, constant k, A, q):
//     -d/dx ( k A dT/dx ) = q A        on 0 < x < L
//     T(0) = T0,  T(L) = TL            (prescribed temperatures)
//
// Discretisation: N uniform two-node linear elements, h = L / N.
//   element stiffness  ke = (k A / h) [  1 -1 ]
//                                     [ -1  1 ]
//   element load       fe = (q A h / 2) [ 1  1 ]^T   (exact for uniform q)
//
// Units (SI throughout): x, L [m]; A [m^2]; k [W/(m K)]; q [W/m^3];
// T [deg C] (only temperature differences enter the equation, so C and K
// are interchangeable for the solve); reaction fluxes [W].
#pragma once

#include "workbench/core/errors.hpp"

#include <cstddef>
#include <vector>

namespace wb::thermal {

struct RodProblem {
    double length = 1.0;               // L  [m]
    double area = 1e-4;                // A  [m^2]
    double conductivity = 10.0;        // k  [W/(m K)]
    double heatGeneration = 10'000.0;  // q  [W/m^3]
    double temperatureLeft = 100.0;    // T(0) [C]
    double temperatureRight = 20.0;    // T(L) [C]

    /// Throws wb::ValidationError with an actionable message if invalid.
    void validate() const;
};

/// Upper bound on elements accepted from user input (keeps memory bounded).
inline constexpr std::size_t kMaxElements = 2'000'000;

struct RodSolution {
    RodProblem problem;
    std::size_t elements = 0;
    std::vector<double> x;            // nodal coordinates, size N+1 [m]
    std::vector<double> temperature;  // nodal temperatures, size N+1 [C]
    double reactionLeft = 0.0;        // boundary heat flow at x=0 [W], + = into rod
    double reactionRight = 0.0;       // boundary heat flow at x=L [W], + = into rod

    double elementSize() const noexcept { return problem.length / static_cast<double>(elements); }
    /// FE interpolation T_h(x) using the linear shape functions of the
    /// element containing x. Throws wb::ValidationError if x is outside [0, L].
    double interpolate(double xQuery) const;
};

/// Exact solution T(x) = T0 + (TL - T0) x / L + q / (2k) x (L - x).
/// With the assignment values this is 100 - 80 x / L + 500 x (L - x).
double exactTemperature(const RodProblem& p, double x) noexcept;

/// Assembles global stiffness K and load F (before boundary conditions).
/// Template so the tests can assemble into a dense matrix and cross-check.
template <typename Matrix>
void assembleRodSystem(const RodProblem& p, std::size_t elements, Matrix& K, std::vector<double>& F) {
    const double h = p.length / static_cast<double>(elements);
    const double kA_h = p.conductivity * p.area / h;
    const double fHalf = 0.5 * p.heatGeneration * p.area * h;
    const double ke[2][2] = {{kA_h, -kA_h}, {-kA_h, kA_h}};
    const double fe[2] = {fHalf, fHalf};
    F.assign(elements + 1, 0.0);
    for (std::size_t e = 0; e < elements; ++e) {
        const std::size_t dof[2] = {e, e + 1};  // element connectivity
        for (int a = 0; a < 2; ++a) {
            F[dof[a]] += fe[a];
            for (int b = 0; b < 2; ++b) K.add(dof[a], dof[b], ke[a][b]);
        }
    }
}

/// Imposes T[node] = value by symmetric elimination: the known value is moved
/// to the right-hand side of every coupled row, then the row and column are
/// replaced by the identity. Keeps K symmetric positive definite.
template <typename Matrix>
void applyPrescribedTemperature(Matrix& K, std::vector<double>& F, std::size_t node, double value) {
    const std::size_t n = K.rows();
    const std::size_t lo = node > 0 ? node - 1 : 0;           // only neighbours couple in 1D
    const std::size_t hi = node + 1 < n ? node + 1 : n - 1;
    for (std::size_t i = lo; i <= hi; ++i) {
        if (i == node) continue;
        F[i] -= K.at(i, node) * value;
        K.set(i, node, 0.0);
        K.set(node, i, 0.0);
    }
    K.set(node, node, 1.0);
    F[node] = value;
}

/// Full solve: validate, assemble, apply both BCs, solve, recover reactions.
/// Throws wb::ValidationError for invalid inputs or element counts.
RodSolution solveRod(const RodProblem& p, std::size_t elements);

struct MidpointErrorReport {
    std::size_t elements = 0;
    double h = 0.0;
    double maxMidpointError = 0.0;    // max_e |T_h(x_mid,e) - T_exact(x_mid,e)| [C]
    double maxNodalError = 0.0;       // reported for contrast only [C]
    double predictedMidpointError = 0.0;  // q h^2 / (8 k), see docs [C]
    std::vector<double> midpointX;
    std::vector<double> midpointFe;
    std::vector<double> midpointExact;
};

MidpointErrorReport evaluateMidpointError(const RodSolution& solution);

struct ConvergenceRow {
    MidpointErrorReport report;
    double observedOrder = 0.0;  // log(e_prev/e)/log(h_prev/h); 0 for the first row
};

/// Solves for every element count (e.g. 10, 20, 40, 80) and reports
/// midpoint errors with observed convergence order between successive meshes.
std::vector<ConvergenceRow> runConvergenceStudy(const RodProblem& p, const std::vector<std::size_t>& meshes);

}  // namespace wb::thermal
