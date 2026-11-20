#include "workbench/ml/backprop.hpp"

#include "workbench/core/errors.hpp"

#include <cmath>

namespace wb::ml {
namespace {

double lossAt(const Parameters& p, const Sample& s) noexcept { return forward(p, s).loss; }

/// Central difference of the loss along one parameter selected by `member`.
double centralDifference(Parameters p, const Sample& s, double Parameters::*member, double eps) noexcept {
    const double original = p.*member;
    p.*member = original + eps;
    const double plus = lossAt(p, s);
    p.*member = original - eps;
    const double minus = lossAt(p, s);
    return (plus - minus) / (2.0 * eps);
}

}  // namespace

ForwardPass forward(const Parameters& p, const Sample& s) noexcept {
    ForwardPass f;
    f.z = p.w * s.x + p.b;
    f.h = std::tanh(f.z);
    f.yHat = p.v * f.h + p.c;
    const double e = f.yHat - s.y;
    f.loss = 0.5 * e * e;
    return f;
}

BackwardTrace backward(const Parameters& p, const Sample& s) noexcept {
    const ForwardPass f = forward(p, s);
    BackwardTrace t;
    t.dJ_dyHat = f.yHat - s.y;
    t.grads.dc = t.dJ_dyHat;
    t.grads.dv = t.dJ_dyHat * f.h;
    t.dJ_dh = t.dJ_dyHat * p.v;
    t.dh_dz = 1.0 - f.h * f.h;
    t.dJ_dz = t.dJ_dh * t.dh_dz;
    t.grads.dw = t.dJ_dz * s.x;
    t.grads.db = t.dJ_dz;
    return t;
}

Gradients numericalGradients(const Parameters& p, const Sample& s, double epsilon) noexcept {
    Gradients g;
    g.dw = centralDifference(p, s, &Parameters::w, epsilon);
    g.db = centralDifference(p, s, &Parameters::b, epsilon);
    g.dv = centralDifference(p, s, &Parameters::v, epsilon);
    g.dc = centralDifference(p, s, &Parameters::c, epsilon);
    return g;
}

Parameters gradientStep(const Parameters& p, const Gradients& g, double learningRate) {
    if (!std::isfinite(learningRate) || learningRate <= 0.0) {
        throw ValidationError("learning rate must be a positive finite number");
    }
    // theta_new = theta_old - eta * dJ/dtheta, all evaluated at theta_old.
    return {p.w - learningRate * g.dw, p.b - learningRate * g.db, p.v - learningRate * g.dv,
            p.c - learningRate * g.dc};
}

std::vector<TrainingRecord> train(const Parameters& initial, const Sample& s, double learningRate,
                                  std::size_t steps) {
    if (steps > 100'000) throw ValidationError("training steps must not exceed 100000");
    std::vector<TrainingRecord> history;
    history.reserve(steps + 1);
    Parameters p = initial;
    history.push_back({0, p, forward(p, s).loss});
    for (std::size_t k = 1; k <= steps; ++k) {
        p = gradientStep(p, backward(p, s).grads, learningRate);
        history.push_back({k, p, forward(p, s).loss});
    }
    return history;
}

}  // namespace wb::ml
