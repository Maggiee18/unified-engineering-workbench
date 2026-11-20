#include "workbench/core/errors.hpp"
#include "workbench/ml/backprop.hpp"

#include <doctest.h>

#include <cmath>

using namespace wb::ml;

TEST_CASE("Backprop: forward pass at the initial parameters") {
    const ForwardPass f = forward(Parameters{}, Sample{});
    CHECK(f.z == doctest::Approx(0.5));
    CHECK(f.h == doctest::Approx(std::tanh(0.5)));
    CHECK(f.h == doctest::Approx(0.46211715726000974));
    CHECK(f.yHat == doctest::Approx(0.46211715726000974));
    CHECK(f.loss == doctest::Approx(0.5 * std::pow(0.46211715726000974 - 2.0, 2)));
}

TEST_CASE("Backprop: analytic gradients match hand-derived values") {
    const BackwardTrace t = backward(Parameters{}, Sample{});
    const double h = std::tanh(0.5);
    const double e = h - 2.0;
    CHECK(t.grads.dc == doctest::Approx(e));
    CHECK(t.grads.dv == doctest::Approx(e * h));
    CHECK(t.grads.dw == doctest::Approx(e * 1.0 * (1 - h * h)));
    CHECK(t.grads.db == doctest::Approx(e * 1.0 * (1 - h * h)));
    // Rounded values used in the handwritten derivation
    CHECK(t.grads.dc == doctest::Approx(-1.537883).epsilon(1e-6));
    CHECK(t.grads.dv == doctest::Approx(-0.710682).epsilon(1e-6));
    CHECK(t.grads.dw == doctest::Approx(-1.209464).epsilon(1e-6));
}

TEST_CASE("Backprop: central finite differences agree within 1e-5 (eps = 1e-6)") {
    const Parameters p;
    const Sample s;
    const Gradients a = backward(p, s).grads;
    const Gradients n = numericalGradients(p, s, 1e-6);
    CHECK(std::fabs(a.dw - n.dw) <= 1e-5);
    CHECK(std::fabs(a.db - n.db) <= 1e-5);
    CHECK(std::fabs(a.dv - n.dv) <= 1e-5);
    CHECK(std::fabs(a.dc - n.dc) <= 1e-5);
}

TEST_CASE("Backprop: one simultaneous update uses gradients of the original parameters") {
    const Parameters p;
    const Gradients g = backward(p, Sample{}).grads;
    const Parameters q = gradientStep(p, g, 0.1);
    CHECK(q.w == doctest::Approx(0.5 - 0.1 * g.dw));
    CHECK(q.b == doctest::Approx(0.0 - 0.1 * g.db));
    CHECK(q.v == doctest::Approx(1.0 - 0.1 * g.dv));
    CHECK(q.c == doctest::Approx(0.0 - 0.1 * g.dc));
    // All gradients are negative, so gradient descent increases every parameter.
    CHECK(q.w > p.w);
    CHECK(q.v > p.v);
    CHECK(forward(q, Sample{}).loss < forward(p, Sample{}).loss);
}

TEST_CASE("Backprop: 100 training steps reduce the loss monotonically") {
    const auto history = train(Parameters{}, Sample{}, 0.1, 100);
    REQUIRE(history.size() == 101);
    for (std::size_t i = 1; i < history.size(); ++i) CHECK(history[i].loss <= history[i - 1].loss);
    CHECK(history.back().loss < 1e-3 * history.front().loss);
}

TEST_CASE("Backprop negative: invalid learning rate") {
    CHECK_THROWS_AS(gradientStep(Parameters{}, Gradients{}, 0.0), wb::ValidationError);
    CHECK_THROWS_AS(gradientStep(Parameters{}, Gradients{}, std::nan("")), wb::ValidationError);
}
