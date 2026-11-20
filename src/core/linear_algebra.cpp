#include "workbench/core/linear_algebra.hpp"

#include "workbench/core/errors.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace wb::la {

// ---------------------------------------------------------------- tridiagonal

TridiagonalMatrix::TridiagonalMatrix(std::size_t n) : lower_(n, 0.0), diag_(n, 0.0), upper_(n, 0.0) {
    if (n == 0) throw std::invalid_argument("matrix dimension must be positive");
}

double& TridiagonalMatrix::ref(std::size_t i, std::size_t j) {
    const std::size_t n = rows();
    if (i >= n || j >= n) throw std::out_of_range("tridiagonal index out of range");
    if (i == j) return diag_[i];
    if (j + 1 == i) return lower_[i];
    if (i + 1 == j) return upper_[i];
    throw std::out_of_range("entry (" + std::to_string(i) + "," + std::to_string(j) +
                            ") is outside the tridiagonal band");
}

void TridiagonalMatrix::add(std::size_t i, std::size_t j, double value) { ref(i, j) += value; }
void TridiagonalMatrix::set(std::size_t i, std::size_t j, double value) { ref(i, j) = value; }

double TridiagonalMatrix::at(std::size_t i, std::size_t j) const {
    const std::size_t n = rows();
    if (i >= n || j >= n) throw std::out_of_range("tridiagonal index out of range");
    if (i == j) return diag_[i];
    if (j + 1 == i) return lower_[i];
    if (i + 1 == j) return upper_[i];
    return 0.0;
}

std::vector<double> TridiagonalMatrix::solve(const std::vector<double>& rhs) const {
    const std::size_t n = rows();
    if (rhs.size() != n) throw std::invalid_argument("rhs size does not match matrix");
    // Forward sweep: c' and d' (modified upper diagonal and rhs).
    std::vector<double> cPrime(n, 0.0), dPrime(n, 0.0);
    double pivot = diag_[0];
    if (pivot == 0.0 || !std::isfinite(pivot)) throw NumericalError("zero pivot at row 0");
    cPrime[0] = upper_[0] / pivot;
    dPrime[0] = rhs[0] / pivot;
    for (std::size_t i = 1; i < n; ++i) {
        pivot = diag_[i] - lower_[i] * cPrime[i - 1];
        if (pivot == 0.0 || !std::isfinite(pivot)) {
            throw NumericalError("zero or non-finite pivot at row " + std::to_string(i));
        }
        cPrime[i] = (i + 1 < n) ? upper_[i] / pivot : 0.0;
        dPrime[i] = (rhs[i] - lower_[i] * dPrime[i - 1]) / pivot;
    }
    // Back substitution.
    std::vector<double> x(n);
    x[n - 1] = dPrime[n - 1];
    for (std::size_t i = n - 1; i-- > 0;) x[i] = dPrime[i] - cPrime[i] * x[i + 1];
    return x;
}

std::vector<double> TridiagonalMatrix::multiply(const std::vector<double>& x) const {
    const std::size_t n = rows();
    if (x.size() != n) throw std::invalid_argument("vector size does not match matrix");
    std::vector<double> y(n, 0.0);
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = diag_[i] * x[i];
        if (i > 0) y[i] += lower_[i] * x[i - 1];
        if (i + 1 < n) y[i] += upper_[i] * x[i + 1];
    }
    return y;
}

// --------------------------------------------------------------------- dense

DenseMatrix::DenseMatrix(std::size_t n) : n_(n), data_(n * n, 0.0) {
    if (n == 0) throw std::invalid_argument("matrix dimension must be positive");
}

std::vector<double> DenseMatrix::solve(const std::vector<double>& rhs) const {
    if (rhs.size() != n_) throw std::invalid_argument("rhs size does not match matrix");
    std::vector<double> a = data_;
    std::vector<double> b = rhs;
    for (std::size_t col = 0; col < n_; ++col) {
        std::size_t best = col;
        for (std::size_t r = col + 1; r < n_; ++r) {
            if (std::fabs(a[r * n_ + col]) > std::fabs(a[best * n_ + col])) best = r;
        }
        if (a[best * n_ + col] == 0.0) throw NumericalError("singular matrix");
        if (best != col) {
            for (std::size_t c = 0; c < n_; ++c) std::swap(a[col * n_ + c], a[best * n_ + c]);
            std::swap(b[col], b[best]);
        }
        for (std::size_t r = col + 1; r < n_; ++r) {
            const double f = a[r * n_ + col] / a[col * n_ + col];
            if (f == 0.0) continue;
            for (std::size_t c = col; c < n_; ++c) a[r * n_ + c] -= f * a[col * n_ + c];
            b[r] -= f * b[col];
        }
    }
    std::vector<double> x(n_);
    for (std::size_t i = n_; i-- > 0;) {
        double s = b[i];
        for (std::size_t c = i + 1; c < n_; ++c) s -= a[i * n_ + c] * x[c];
        x[i] = s / a[i * n_ + i];
    }
    return x;
}

std::vector<double> DenseMatrix::multiply(const std::vector<double>& x) const {
    if (x.size() != n_) throw std::invalid_argument("vector size does not match matrix");
    std::vector<double> y(n_, 0.0);
    for (std::size_t i = 0; i < n_; ++i)
        for (std::size_t j = 0; j < n_; ++j) y[i] += data_[i * n_ + j] * x[j];
    return y;
}

}  // namespace wb::la
