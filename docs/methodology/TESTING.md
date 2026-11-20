# Testing strategy and outcomes

Framework: doctest 2.4.11 (single header, MIT). One executable, `workbench_tests`,
registered with CTest. 62 test cases, 1276 assertions. Logs in `results/test_logs/`.

## Outcomes (actual runs)

| Where | Binary | Result |
|---|---|---|
| Cloud sandbox, Linux, GCC 13.3 `-O3` | `build-linux/workbench_tests` via `ctest` | 62/62 passed, 1276/1276 assertions |
| Cloud sandbox, Windows binary under Wine | `build-win/workbench_tests.exe` (MinGW-w64 GCC 13, static) | 62/62 passed |
| **Target laptop, native Windows 11 (build 26300)** | packaged `bin\workbench_tests.exe` started by double clicking `bin\RunTests.cmd` | **62/62 passed, 1276/1276 assertions** |

End to end UI checks: `tools/ui_walkthrough.mjs` (Playwright, headless Chromium)
drives every view against a running backend: 17 steps, 0 browser console errors.
It was run against the Linux build and against the Windows `Launch.exe` under Wine;
see `docs/walkthrough/`. On the target laptop `Launch.exe` was started by double
click and the UI loaded (screenshot in `docs/walkthrough/windows_native/`).

## What is covered

Positive
* Valid ASCII STL (single facet, mixed case keywords, CRLF, scientific notation,
  example file); valid CSV (BOM, comments, reordered columns, quotes, CRLF, example).
* FEA: hand computed 2 element K/F, BC values exact, K symmetric, nodal exactness,
  q = 0 linear profile, interpolation via shape functions.
* Thread pool returns values and propagates exceptions; parallel batch in input order.
* Mock devices: discovery, capabilities, connect, telemetry sequence, disconnect.
* Backprop forward pass, gradients, one update, 100 step training.

Negative
* Empty / whitespace only / header only files; malformed STL structure (7 variants);
  binary STL; missing/duplicate CSV columns; wrong field counts.
* Non numeric values, NaN, Inf, overflow, non integer and non positive node ids,
  temperatures below absolute zero, duplicate node ids, size/count limits.
* Invalid thermal parameters (k ≤ 0, L < 0, NaN area, T < −273.15 °C, N = 0 or too large).
* Zero workers, worker list of wrong type, negative or fractional case counts,
  zero elements, zero repetitions, memory guard (JSON transport layer tests).
* Device timeout (host waits its full deadline), telemetry while faulted or
  disconnected, unknown device id or fault name, invalid learning rate.
* Cancellation of serial and parallel runs publishes no partial results.

Numerical
* Analytical vs FE: midpoint error = q h²/(8k) for N = 10, 20, 40, 80 (relative 1e-9).
* Observed convergence order 2.0 ± 1e-6.
* Tridiagonal solver vs independent dense Gaussian elimination (1e-12).
* Energy balance: R(0) + R(L) = −qAL, R(0) = −0.42 W, R(L) = −0.58 W.
* Serial vs parallel max |ΔT| ≤ 1e-8 °C for 1 to 4 workers (observed 0).
* Reproducible batch: identical cases for the same seed; first draw pinned.
* Central finite differences (ε = 1e-6) agree with analytic gradients within 1e-5
  (observed about 1e-10).

## Defects the tests caught during development

* A test expectation used a hand rounded gradient (−1.209470) that was wrong in
  the 6th digit (correct −1.209464); a loose tolerance had hidden it. Tolerance
  tightened and value corrected.
* A malformed STL test truncated text in the middle of a line, so the parser
  (correctly) reported a different error than the test expected. Test fixed.
* The end to end walkthrough found that the Performance "Run benchmark" button did
  nothing: an input named `elements` shadowed `HTMLFormElement.elements`. Renamed.
* Launching the app twice on the target PC showed both backends listening on the
  same port 8765: the HTTP library's default socket options (SO_REUSEADDR on Windows,
  SO_REUSEPORT on Linux) allow that, and a failed bind also left the server unable to
  try the next port. Fixed with exclusive binding plus a reset between attempts;
  re-tested with two instances (8765 and 8766) on Linux and on the target PC.
* Measuring size limits with curl found an 8 KiB cap on form urlencoded uploads in
  the HTTP library and a 413 without a JSON body. Both fixed (see git log).

## How to run

* Windows, packaged: double click `bin\RunTests.cmd` (writes `bin\test_output.txt`).
* From source: `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release`,
  `cmake --build build`, `ctest --test-dir build --output-on-failure`.
* UI walkthrough: start `Launch.exe --no-browser --port 8765`, then
  `node tools/ui_walkthrough.mjs http://127.0.0.1:8765/ docs/walkthrough` (needs
  Node and Playwright; development only).
