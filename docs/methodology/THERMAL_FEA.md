# Thermal FEA: formulation, verification and learning notes (Task C)

Code: `include/workbench/thermal/rod_fea.hpp`, `src/thermal/rod_fea.cpp`,
`src/core/linear_algebra.cpp`. Tests: `tests/test_thermal_fea.cpp`.
Raw data: `results/csv/fea_*.csv`. Plots: `results/plots/fea_*.png`.

## 1. Problem

A uniform rod, insulated on its lateral surface, generates heat uniformly and has
both ends held at fixed temperatures. Heat can only leave through the ends.

| Symbol | Meaning | Value | Unit |
|---|---|---|---|
| L | length | 1 | m |
| A | cross section area | 1e-4 | m² |
| k | thermal conductivity | 10 | W/(m·K) |
| q | volumetric heat generation | 10 000 | W/m³ |
| T(0) | left end temperature | 100 | °C |
| T(L) | right end temperature | 20 | °C |

Governing equation (steady state, energy balance on a slice dx):

  −d/dx ( k A dT/dx ) = q A,   0 < x < L

Assumptions: steady state; k, A, q constant; no heat loss through the lateral
surface (so the problem is truly 1D); temperature only varies with x. Because only
temperature differences appear, °C and K are interchangeable in the solve.

## 2. Exact solution (used as the reference)

Integrating twice with k, A, q constant: T(x) = −q x² / (2k) + C1 x + C2. The boundary
conditions give C2 = T0 and C1 = (TL − T0)/L + qL/(2k), so

  T_exact(x) = T0 + (TL − T0) x / L + q/(2k) · x (L − x)
             = 100 − 80 x / L + 500 x (L − x)      (assignment values)

The maximum is at x = 0.42 m, T = 188.2 °C. Note that A cancels: it scales both the
conduction and the generation term.

## 3. Finite element formulation

Weak form: multiply by a test function w with w = 0 where T is prescribed, integrate
by parts:  ∫ k A T′ w′ dx = ∫ q A w dx  (+ boundary flux terms at constrained nodes).

Mesh: N two node linear elements of length h = L/N, nodes x_i = i h. Inside element e
the temperature is interpolated with the shape functions N1 = 1 − ξ, N2 = ξ,
ξ = (x − x_e)/h:

  T_h(x) = N1 T_e + N2 T_{e+1}

Element matrices (exact integration for constant k, A, q):

  k^e = (k A / h) [  1  −1 ]        f^e = (q A h / 2) [ 1 ]
                  [ −1   1 ]                          [ 1 ]

With the assignment values and N = 10: kA/h = 10 × 1e-4 / 0.1 = 0.01 W/K and
qAh/2 = 10 000 × 1e-4 × 0.1 / 2 = 0.05 W.

Assembly: for each element, add k^e into rows/columns (e, e+1) of K and f^e into
F. Because each element only couples two neighbouring nodes, K is tridiagonal; the
code stores only the three diagonals (`TridiagonalMatrix`) and solves with the
Thomas algorithm in O(N). The assembly function is a template so the tests can also
assemble into a dense matrix and solve with Gaussian elimination as an independent
check.

Boundary conditions (Dirichlet, both ends) by **symmetric elimination**: for a
prescribed node p with value g, every coupled row i gets F_i −= K_ip g, then row p
and column p are zeroed, K_pp = 1 and F_p = g. This keeps K symmetric positive
definite, imposes T exactly (the tests check T(0) = 100 and T(L) = 20 bit for bit),
and does not depend on a penalty number.

Reactions (heat flow through the ends): before the BCs are applied the code keeps the
two boundary rows. After solving, R = K_row · T − F_row is the heat flowing **into** the
rod at that end [W]. Results: R(0) = −0.42 W and R(L) = −0.58 W, i.e. 0.42 W leaves on
the left and 0.58 W on the right, and R(0) + R(L) = −qAL = −1 W. That energy balance
holds to about 1e-15 W and is shown in the UI.

## 4. Error measure: midpoints with FE interpolation

The assignment asks for the maximum absolute error at element midpoints using FE
interpolation, not only at nodes. For each element the code evaluates
T_h(x_mid) = (T_e + T_{e+1})/2 through `RodSolution::interpolate()` (the general shape
function path, not a shortcut) and compares it with T_exact(x_mid).

## 5. Results

| N | h [m] | max midpoint error [°C] | q h²/(8k) [°C] | max nodal error [°C] | observed order |
|---|---|---|---|---|---|
| 10 | 0.1 | 1.250000 | 1.25 | 2.8e-14 | |
| 20 | 0.05 | 0.312500 | 0.3125 | 2.0e-13 | 2.0000 |
| 40 | 0.025 | 0.078125 | 0.078125 | 8.8e-13 | 2.0000 |
| 80 | 0.0125 | 0.019531 | 0.01953125 | 1.2e-12 | 2.0000 |

(Values from `results/csv/fea_convergence.csv`, produced by `workbench_cli fea`.)

* Error drops by a factor of 4 each time h halves: **second order convergence**,
  as expected for linear elements in the L∞ sense.
* The midpoint error equals q h²/(8k) **exactly**. Reason: the nodal values are exact
  (next section) and the exact solution is a parabola with T″ = −q/k, so the gap
  between a parabola and its chord at the chord's midpoint is (h²/8)|T″|. This gives
  a closed form oracle that the tests use with a 1e-9 relative tolerance.

## 6. Debugging insight

The first solver run showed nodal errors around 1e-13 °C for every mesh, even
N = 10. That looks like a perfect solver, and an error check done only at the nodes
would have reported "converged, zero error" and hidden the real discretisation
error. The explanation is a known property of 1D linear elements: for this equation
the Green's function is piecewise linear, so the FE solution interpolates the exact
solution at the nodes (nodal superconvergence). The real error lives **between** the
nodes, which is exactly why the brief asks for midpoints evaluated through the shape
functions. The nodal error column is kept in the table and the plot on purpose to show
the contrast.

A second, smaller one: the reaction sign. The first expectation was +0.42 W at
x = 0. Writing out the weak form showed the boundary term is −kA T′(0), and T′(0) =
+420 K/m, so heat flows out (−0.42 W). The test now pins both reactions and their sum.

## 7. Verification summary

* Hand computed 2 element K and F (tests).
* Tridiagonal solve equals dense Gaussian elimination for N = 1, 3, 10, 37.
* K symmetric after BCs; BC values imposed exactly.
* Nodal exactness (< 1e-9 °C), midpoint error = q h²/(8k) for N = 10 to 80,
  observed order 2.0 ± 1e-6.
* Energy balance: R(0) + R(L) = −qAL within 1e-10 relative.
* q = 0 gives a linear profile with zero error.
* Independent cross check of the closed form numbers in Python (outside the
  application, see AI_USE_LOG.md).
* Invalid inputs (k ≤ 0, L ≤ 0, NaN area, T < −273.15 °C, N = 0, N > 2 000 000) are
  rejected with messages.

## 8. Interdisciplinary note

Coming from AI/ML rather than mechanical engineering, the parts that transfer
directly are the linear algebra (sparse/banded systems, why
Thomas is O(N)), convergence as an empirical rate (log-log slope, like learning
curves) and testing against an analytical oracle. The new ideas are the weak form,
why Dirichlet conditions must be imposed on the assembled system, and treating
reactions as a physical check (energy balance) rather than just a byproduct.
