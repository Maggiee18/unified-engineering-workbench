// Hand-derived backpropagation for the single-sample network of Task F.
//
//   z     = w x + b
//   h     = tanh(z)
//   y_hat = v h + c
//   J     = 0.5 (y_hat - y)^2
//
// Chain rule (no automatic differentiation):
//   dJ/dy_hat = (y_hat - y)
//   dJ/dc     = dJ/dy_hat * 1
//   dJ/dv     = dJ/dy_hat * h
//   dJ/dh     = dJ/dy_hat * v
//   dh/dz     = 1 - tanh(z)^2 = 1 - h^2
//   dJ/dz     = dJ/dh * (1 - h^2)
//   dJ/dw     = dJ/dz * x
//   dJ/db     = dJ/dz * 1
#pragma once

#include <cstddef>
#include <vector>

namespace wb::ml {

struct Parameters {
    double w = 0.5;
    double b = 0.0;
    double v = 1.0;
    double c = 0.0;
};

struct Sample {
    double x = 1.0;
    double y = 2.0;
};

struct ForwardPass {
    double z = 0.0;
    double h = 0.0;
    double yHat = 0.0;
    double loss = 0.0;
};

struct Gradients {
    double dw = 0.0;
    double db = 0.0;
    double dv = 0.0;
    double dc = 0.0;
};

/// Intermediate derivatives, exposed so the UI/report can show every step.
struct BackwardTrace {
    double dJ_dyHat = 0.0;
    double dJ_dh = 0.0;
    double dh_dz = 0.0;
    double dJ_dz = 0.0;
    Gradients grads;
};

ForwardPass forward(const Parameters& p, const Sample& s) noexcept;
BackwardTrace backward(const Parameters& p, const Sample& s) noexcept;

/// Central finite differences: (J(theta+eps) - J(theta-eps)) / (2 eps).
Gradients numericalGradients(const Parameters& p, const Sample& s, double epsilon = 1e-6) noexcept;

/// One simultaneous update: every parameter uses gradients from the *same*
/// (old) parameter set. Throws wb::ValidationError for non-positive/non-finite rate.
Parameters gradientStep(const Parameters& p, const Gradients& g, double learningRate);

struct TrainingRecord {
    std::size_t step = 0;  // 0 = initial parameters
    Parameters params;
    double loss = 0.0;
};

/// Records the initial state and then `steps` further updates.
std::vector<TrainingRecord> train(const Parameters& initial, const Sample& s, double learningRate, std::size_t steps);

}  // namespace wb::ml
