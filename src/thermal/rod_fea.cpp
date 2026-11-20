#include "workbench/thermal/rod_fea.hpp"

#include "workbench/core/linear_algebra.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <string>

namespace wb::thermal {
namespace {

void requirePositiveFinite(double value, const char* name, const char* unit) {
    if (!std::isfinite(value)) throw ValidationError(std::string(name) + " must be a finite number");
    if (value <= 0.0) {
        std::ostringstream msg;
        msg << name << " must be greater than zero (got " << value << ' ' << unit << ')';
        throw ValidationError(msg.str());
    }
}

}  // namespace

void RodProblem::validate() const {
    requirePositiveFinite(length, "Length L", "m");
    requirePositiveFinite(area, "Cross-section area A", "m^2");
    requirePositiveFinite(conductivity, "Thermal conductivity k", "W/(m K)");
    if (!std::isfinite(heatGeneration)) throw ValidationError("Heat generation q must be a finite number");
    if (!std::isfinite(temperatureLeft) || !std::isfinite(temperatureRight)) {
        throw ValidationError("Boundary temperatures must be finite numbers");
    }
    if (temperatureLeft < -273.15 || temperatureRight < -273.15) {
        throw ValidationError("Boundary temperatures cannot be below absolute zero (-273.15 C)");
    }
}

double exactTemperature(const RodProblem& p, double x) noexcept {
    const double L = p.length;
    return p.temperatureLeft + (p.temperatureRight - p.temperatureLeft) * x / L +
           p.heatGeneration / (2.0 * p.conductivity) * x * (L - x);
}

double RodSolution::interpolate(double xQuery) const {
    const double L = problem.length;
    // Allow a few ulps of slack so x = L computed as N*h is accepted.
    const double slack = 1e-12 * L;
    if (!(xQuery >= -slack && xQuery <= L + slack)) {
        throw ValidationError("interpolation point x = " + std::to_string(xQuery) + " m lies outside [0, L]");
    }
    const double h = elementSize();
    std::size_t e = static_cast<std::size_t>(std::floor(std::clamp(xQuery, 0.0, L) / h));
    if (e >= elements) e = elements - 1;  // x == L belongs to the last element
    const double xi = (xQuery - x[e]) / h;  // local coordinate in [0, 1]
    const double N1 = 1.0 - xi;             // linear shape functions
    const double N2 = xi;
    return N1 * temperature[e] + N2 * temperature[e + 1];
}

RodSolution solveRod(const RodProblem& p, std::size_t elements) {
    p.validate();
    if (elements < 1) throw ValidationError("Number of elements must be at least 1");
    if (elements > kMaxElements) {
        throw ValidationError("Number of elements must not exceed " + std::to_string(kMaxElements));
    }
    const std::size_t nodes = elements + 1;
    la::TridiagonalMatrix K(nodes);
    std::vector<double> F;
    assembleRodSystem(p, elements, K, F);

    // Keep the unconstrained boundary rows for reaction (heat-flow) recovery.
    const double k00 = K.at(0, 0), k01 = nodes > 1 ? K.at(0, 1) : 0.0, f0 = F.front();
    const double kNN = K.at(nodes - 1, nodes - 1), kNm = K.at(nodes - 1, nodes - 2), fN = F.back();

    applyPrescribedTemperature(K, F, 0, p.temperatureLeft);
    applyPrescribedTemperature(K, F, nodes - 1, p.temperatureRight);

    RodSolution s;
    s.problem = p;
    s.elements = elements;
    s.temperature = K.solve(F);
    s.x.resize(nodes);
    const double h = p.length / static_cast<double>(elements);
    for (std::size_t i = 0; i < nodes; ++i) s.x[i] = static_cast<double>(i) * h;
    s.x.back() = p.length;  // avoid round-off drift at the right end

    // Reaction R = (K T - F) on the constrained rows = heat entering the rod.
    s.reactionLeft = k00 * s.temperature[0] + k01 * s.temperature[1] - f0;
    s.reactionRight = kNm * s.temperature[nodes - 2] + kNN * s.temperature[nodes - 1] - fN;
    return s;
}

MidpointErrorReport evaluateMidpointError(const RodSolution& s) {
    MidpointErrorReport r;
    r.elements = s.elements;
    r.h = s.elementSize();
    r.predictedMidpointError = std::fabs(s.problem.heatGeneration) * r.h * r.h / (8.0 * s.problem.conductivity);
    for (std::size_t i = 0; i < s.x.size(); ++i) {
        r.maxNodalError = std::max(r.maxNodalError, std::fabs(s.temperature[i] - exactTemperature(s.problem, s.x[i])));
    }
    r.midpointX.reserve(s.elements);
    for (std::size_t e = 0; e < s.elements; ++e) {
        const double xm = 0.5 * (s.x[e] + s.x[e + 1]);
        const double fe = s.interpolate(xm);  // FE interpolation, not nodal values
        const double ex = exactTemperature(s.problem, xm);
        r.midpointX.push_back(xm);
        r.midpointFe.push_back(fe);
        r.midpointExact.push_back(ex);
        r.maxMidpointError = std::max(r.maxMidpointError, std::fabs(fe - ex));
    }
    return r;
}

std::vector<ConvergenceRow> runConvergenceStudy(const RodProblem& p, const std::vector<std::size_t>& meshes) {
    if (meshes.empty()) throw ValidationError("Provide at least one mesh size");
    std::vector<ConvergenceRow> rows;
    for (std::size_t n : meshes) {
        ConvergenceRow row;
        row.report = evaluateMidpointError(solveRod(p, n));
        if (!rows.empty()) {
            const auto& prev = rows.back().report;
            if (prev.maxMidpointError > 0.0 && row.report.maxMidpointError > 0.0) {
                row.observedOrder = std::log(prev.maxMidpointError / row.report.maxMidpointError) /
                                    std::log(prev.h / row.report.h);
            }
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

}  // namespace wb::thermal
