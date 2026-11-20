# Multicore batch performance (Task D)

Code: `src/performance/thread_pool.cpp`, `src/performance/batch.cpp`,
`src/performance/system_info.cpp`. Tests: `tests/test_performance.cpp`.
Raw data: `results/benchmarks/<machine>/`. Plots: `results/plots/benchmark_*.png`.

## 1. Workload

* A batch is `cases` independent copies of the Task C rod problem. L, A, T(0), T(L)
  stay at the assignment values; k and q vary per case.
* Generator: `std::mt19937_64(seed)` with the default seed 20262027. Each draw is
  mapped to [0, 1) with `(x >> 11) * 2^-53` instead of
  `std::uniform_real_distribution`, because the distribution's algorithm is
  implementation defined (MSVC, libstdc++ and libc++ can differ) while mt19937_64 is
  fully specified. A test pins the first value so Linux GCC and Windows MinGW
  produce identical cases.
* k ∈ [1, 400] W/(m·K), q ∈ [1e3, 1e5] W/m³, both positive.
* Each case: assemble, apply BCs, Thomas solve, then evaluate midpoint errors with
  FE interpolation (the same `solveCase()` function in both paths).
* Default size used for the reported numbers: 2000 cases × 2000 elements.

## 2. Serial and parallel implementations

* Serial: a plain loop over the cases.
* Parallel: a **bounded** `ThreadPool` with exactly p `std::thread` workers created
  before timing starts. The cases are split into about 8 contiguous chunks per
  worker; each chunk is one task. Every task writes only its own slice
  `results[begin, end)`, which is preallocated, so no locks are needed and the
  output is in input order no matter which thread finishes first.
* The caller waits for **all** futures before rethrowing any error, so no task can
  outlive the results vector (no dangling references on failure or cancel).
* Cancellation: a shared `std::atomic<bool>` checked before each case; a cancelled
  run throws `OperationCancelled` and the job publishes nothing.

## 3. Measurement protocol

* Timer: `std::chrono::steady_clock`, wall clock.
* Timed region: dispatch + compute + join only. Excluded: batch generation,
  thread creation, file loading, JSON/CSV writing, rendering, compilation.
* For each configuration (serial, p = 1, 2, 3, 4): 1 untimed warm-up run, then 5
  measured runs, report the **median**.
* p is capped at the number of logical CPUs reported by the OS; larger requests are
  marked "skipped" instead of oversubscribing.
* Speedup S_p = T1 / Tp with T1 = the 1 worker pool run (same code path), efficiency
  E_p = S_p / p. The table also reports speedup against the plain serial loop.
* Agreement: max |T_serial − T_parallel| over every node of every case must be
  ≤ 1e-8 °C. In practice it is exactly 0: the same function runs on the same inputs
  in the same floating point order, only on a different thread.
* Launch-to-ready is measured separately: Windows process creation time
  (`GetProcessTimes`) to "socket listening" (backend ready) and to the first UI
  request (UI ready). It is shown in the sidebar and the Host card.

## 4. Results

### 4.1 Cloud sandbox (Linux build, measured 2026-11-01)

Host: Intel Xeon @ 2.10 GHz (cloud VM), 2 physical / 2 logical CPUs, 7.8 GiB RAM,
GCC 13.3.0, `-O3 -DNDEBUG`, Release. 2000 cases × 2000 elements, seed 20262027,
1 warm-up + 5 runs.

| Mode | p | 5 runs [s] | median [s] | S = T1/Tp | E | max abs ΔT vs serial |
|---|---|---|---|---|---|---|
| serial loop | 1 | 0.402, 0.176, 0.176, 0.177, 0.176 | 0.1763 | | | reference |
| pool | 1 | 0.352, 0.182, 0.199, 0.231, 0.185 | 0.1992 | 1.00 | 1.00 | 0 °C |
| pool | 2 | 0.122, 0.109, 0.099, 0.102, 0.099 | 0.1016 | 1.96 | 0.98 | 0 °C |
| pool | 3, 4 | skipped: only 2 logical CPUs | | | | |

Source: `results/benchmarks/cloud_linux_xeon_2vcpu/`.

### 4.2 Target laptop: Windows 11, packaged build (measured 2026-11-01)

Host: Intel Core i5-14450HX (hybrid: 6 performance + 4 efficiency cores),
10 physical / 16 logical CPUs, 24 GiB RAM, Windows 11 build 26300. Packaged
`bin\workbench_cli.exe` (MinGW-w64 GCC 13, `-O3 -DNDEBUG`, static), started by
double clicking `bin\RunBenchmark.cmd` in the extracted ZIP. Same workload and
protocol as above. The Launch.exe backend was idle in the background.

| Mode | p | 5 runs [s] | median [s] | S = T1/Tp | E | max abs ΔT vs serial |
|---|---|---|---|---|---|---|
| serial loop | 1 | 0.1119, 0.1121, 0.1138, 0.1189, 0.1170 | 0.1138 | | | reference |
| pool | 1 | 0.1136, 0.1146, 0.1180, 0.1239, 0.1037 | 0.1146 | 1.00 | 1.00 | 0 °C |
| pool | 2 | 0.0584, 0.0552, 0.0571, 0.0556, 0.0563 | 0.0563 | 2.04 | 1.02 | 0 °C |
| pool | 3 | 0.0422, 0.0444, 0.0440, 0.0408, 0.0404 | 0.0422 | 2.72 | 0.91 | 0 °C |
| pool | 4 | 0.0355, 0.0309, 0.0304, 0.0347, 0.0315 | 0.0315 | 3.63 | 0.91 | 0 °C |

Source: `results/benchmarks/windows11_i5-14450HX/`.

## 5. Analysis (honest reading of the numbers)

* On the target laptop the speedup is 2.04, 2.72 and 3.63 for 2, 3 and 4 workers
  (efficiency 1.02, 0.91, 0.91). Serial and parallel results are bitwise identical.
* The 1.02 efficiency at p = 2 is not real "superlinear" speedup: the five p = 1 runs
  spread from 0.104 to 0.124 s (about ±9 %), so a 2 % difference is inside the noise.
  Turbo frequency also drops as more cores become busy, which works the other way.
* The drop to about 0.91 at p = 3 and 4 is expected: chunk scheduling and joining
  cost a fixed amount per run (more visible when a whole run takes only 30 ms), and on
  a hybrid CPU Windows may place a worker on an efficiency core, which is slower; the
  run finishes when the slowest chunk does. Larger batches would amortise the fixed
  overhead better. The pool p = 1 run costs only 0.7 % more than the serial loop
  here, so the pool itself is cheap.
* In the 2 vCPU cloud VM, with 2 workers the batch ran 1.96 times faster than with 1 worker (98 %
  efficiency). That is close to ideal because the cases are fully independent,
  similar in cost, and each one is small enough (16 KB of temperatures) to live in
  cache, so the threads do not compete much for memory bandwidth.
* The first measured run of every configuration was about twice as slow as the rest
  (0.40 s vs 0.18 s). One warm-up run was not enough on this VM; the most likely
  causes are first touch page faults of the freshly allocated result vectors and CPU
  frequency/noisy neighbour effects of a shared cloud host. The median is robust to
  this single outlier, which is why it is the reported statistic.
* The 1 worker pool was about 13 % slower than the plain serial loop (0.199 s vs
  0.176 s). Some of this is real overhead (task queue, `std::function`,
  `packaged_task`, results written by another thread) but the spread of the pool
  runs (0.18 to 0.23 s) shows it is mostly within the run to run noise of this VM.
  For that reason speedup is reported against the pool p = 1 (same code path) as the
  brief defines it, with the serial comparison shown separately.
* Limits: this VM only exposes 2 logical CPUs, so p = 3 and 4 could not be measured
  here and are reported as skipped rather than run oversubscribed. On a machine with
  SMT, worker counts above the physical core count share execution units and should
  not be expected to scale linearly. No minimum speedup is claimed.

## 6. Reproduce

* UI: Performance view, defaults, Run benchmark; export CSV/JSON/PNG.
* Headless: `bin\RunBenchmark.cmd` (Windows) or
  `workbench_cli benchmark results 2000 2000 <machine_label>`.
