// Minimal linear-algebra containers for the 1D FEA solver.
//
// Two matrix types share one assembly interface (rows(), add(), at(), set()):
//   * TridiagonalMatrix - O(n) storage; the production path. Two-node linear
//     elements on a 1D mesh only couple neighbouring nodes, so the global
//     stiffness matrix is exactly tridiagonal.
//   * DenseMatrix - O(n^2) storage with partial-pivot Gaussian elimination;
//     used by the tests as an independent cross-check of assembly and solve.
#pragma once

#include <cstddef>
#include <vector>

namespace wb::la {

class TridiagonalMatrix {
public:
    explicit TridiagonalMatrix(std::size_t n);

    std::size_t rows() const noexcept { return diag_.size(); }

    /// Adds `value` to entry (i, j). Throws std::out_of_range if |i - j| > 1.
    void add(std::size_t i, std::size_t j, double value);
    void set(std::size_t i, std::size_t j, double value);
    /// Entry (i, j); zero outside the band.
    double at(std::size_t i, std::size_t j) const;

    /// Solves A x = b with the Thomas algorithm (no pivoting). Valid for the
    /// diagonally dominant SPD systems produced by FEA; throws
    /// wb::NumericalError if a pivot is zero or non-finite.
    std::vector<double> solve(const std::vector<double>& rhs) const;

    /// y = A x
    std::vector<double> multiply(const std::vector<double>& x) const;

private:
    double& ref(std::size_t i, std::size_t j);
    std::vector<double> lower_;  // lower_[i] = A(i, i-1), lower_[0] unused
    std::vector<double> diag_;   // diag_[i]  = A(i, i)
    std::vector<double> upper_;  // upper_[i] = A(i, i+1), upper_[n-1] unused
};

class DenseMatrix {
public:
    explicit DenseMatrix(std::size_t n);

    std::size_t rows() const noexcept { return n_; }
    void add(std::size_t i, std::size_t j, double value) { data_[i * n_ + j] += value; }
    void set(std::size_t i, std::size_t j, double value) { data_[i * n_ + j] = value; }
    double at(std::size_t i, std::size_t j) const { return data_[i * n_ + j]; }

    /// Gaussian elimination with partial pivoting.
    std::vector<double> solve(const std::vector<double>& rhs) const;
    std::vector<double> multiply(const std::vector<double>& x) const;

private:
    std::size_t n_;
    std::vector<double> data_;
};

}  // namespace wb::la
