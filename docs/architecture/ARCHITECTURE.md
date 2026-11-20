# Architecture and design decisions

## 1. Overview

```
 Launch.exe  (one static Windows x64 executable, no DLLs beyond Windows system DLLs)
 ├── src/app/main.cpp            start backend on 127.0.0.1, open Edge app window
 ├── Transport  src/server/      cpp-httplib routes, JSON mapping, job manager,
 │                               embedded UI assets, structured errors
 ├── C++ core   src/{core,geometry,thermal,performance,hardware,ml,utilities}
 │              include/workbench/...   (no HTTP, JSON or UI dependencies)
 └── Presentation src/ui/        HTML/CSS/JS (three.js, Chart.js): draws only
```

The **core** is a static library (`workbench_core`) used by three front ends: the
application (`Launch`), the headless generator (`workbench_cli`) and the test suite
(`workbench_tests`). The browser never computes engineering results: files are posted
verbatim to C++ parsers, solver/benchmark/mock/backprop requests go to C++
endpoints, and the JS only renders arrays the backend returns.

## 2. Module map

| Folder | Responsibility | Key types |
|---|---|---|
| `include/workbench/core`, `src/core` | errors, banded and dense linear algebra | `ValidationError`, `ParseError`, `TridiagonalMatrix`, `DenseMatrix` |
| `geometry` | ASCII STL parser (state machine) | `StlMesh`, `parseAsciiStl` |
| `thermal` | nodal CSV parser; 1D FEA solver | `NodalDataset`, `RodProblem`, `solveRod`, `runConvergenceStudy` |
| `performance` | bounded thread pool, batch generator, serial/parallel runners, benchmark protocol, host info | `ThreadPool`, `runBenchmark`, `SystemInfo` |
| `hardware` | adapter interface, device manager, three mocks | `IDeviceAdapter`, `DeviceManager` |
| `ml` | hand derived backprop, finite differences, training loop | `forward`, `backward`, `numericalGradients` |
| `utilities` | locale independent parsing, CSV export, file IO | `parseDouble`, `feaNodalCsv` |
| `server` | HTTP routes, JSON mapping, background jobs, browser launch, embedded assets | `WorkbenchServer`, `JobManager` |
| `ui` | four required views + backprop view | `geometry.js`, `thermal.js`, `performance.js`, `hardware.js`, `backprop.js` |
| `app` | entry points | `main.cpp` (Launch), `cli_main.cpp` |

## 3. Technology choices

| Decision | Chosen | Alternatives considered | Reason |
|---|---|---|---|
| Language/standard | C++17 | C++20 | Required minimum; C++17 is fully supported by both GCC 13 and MinGW-w64 13 and avoids toolchain risk |
| UI | local web UI served by the C++ backend, shown in an Edge `--app` window | Qt 6 Widgets/OpenGL; Dear ImGui | The brief accepts a web UI. Qt 6 for Windows could not be cross built and deployed reliably from the Linux build environment, and the launch gate is a hard shortlisting condition. ImGui would need a custom plotting/3D stack. Edge ships with every Windows 11, so no runtime install is needed. |
| HTTP | cpp-httplib 0.18.3 (header only, MIT) | Boost.Beast, Crow | One header, works with MinGW, no build step |
| JSON | nlohmann/json 3.11.3 (MIT) | RapidJSON | readability, exceptions on misuse |
| 3D | three.js r170 (MIT) + OrbitControls | raw WebGL | robust rotate/pan/zoom and picking |
| Plots | Chart.js 4.4.7 (MIT) | Plotly | small, offline, exports PNG |
| Tests | doctest 2.4.11 (MIT) | GoogleTest | single header, fast compile |
| Build | CMake ≥ 3.20 + Ninja, MinGW-w64 toolchain file | MSVC | static linking gives a single dependency free exe |
| Concurrency | `std::thread` pool with futures | OpenMP | explicit bounded pool, exception propagation, cancellation, no runtime DLL |

All third party code is vendored with licence files in `dependencies/` and versions
and checksums in `dependencies/VERSIONS.md`. The UI files are compiled into the
executable at build time (`tools/embed_assets.cmake`), so nothing is fetched at run
time and the app works offline.

## 4. Runtime behaviour

* `Launch.exe` binds the first free port from 8765 on **127.0.0.1 only** (no firewall
  prompt, not reachable from the network), measures launch-to-ready, and opens
  `msedge --app=http://127.0.0.1:<port>/ --start-maximized`; if Edge is missing it
  falls back to the default browser. The console window is the backend log; closing
  it, or "Quit workbench" in the UI, stops the app.
* Data paths are resolved relative to the executable (`GetModuleFileNameW`), never
  the working directory, so double click, shortcuts and editor tasks behave the same.
* Long computations (benchmark) run as background jobs with progress and
  cooperative cancellation; results are published only on completion. Short ones
  (FEA, parsing) return directly; the UI shows progress and stays responsive
  because `fetch` is asynchronous and the HTTP server is multi threaded.
* Errors: `ParseError` → 422 with line number, `ValidationError` → 400,
  `DeviceStateError` → 409, `DeviceTimeoutError` → 504 with `recoverable`,
  oversize → 413. The UI shows the message next to the action that caused it and in
  the log panel, and keeps the previous valid model on screen.

## 5. Requirements traceability matrix

| # | Requirement (brief section) | Implementation | Verification evidence | Location |
|---|---|---|---|---|
| A1 | One interface with Geometry, Thermal, Performance, Hardware views | `src/ui/index.html`, `src/ui/js/*.js` | walkthrough 17 steps | `docs/walkthrough/` |
| A2 | File selection, validation, progress/status, actionable errors, export | file inputs, `readNumbers()`, status bar/log, CSV/JSON/PNG export | walkthrough steps 4, 5, 7, 9, 10 | same |
| A3 | Responsive UI; cancel or safe completion | `JobManager`, `CancelToken` | test "Cancellation stops...", walkthrough step 12 | `tests/test_performance.cpp` |
| A4 | Core separated from presentation/transport | `workbench_core` has no HTTP/JSON deps | CMake targets | `CMakeLists.txt` |
| A5 | Double click Launch.exe, no terminal/admin/install/build | static MinGW exe, embedded UI; startup written to logs/launch.log | native Windows 11 launch screenshot and launch.log | `docs/walkthrough/windows_native/` |
| A6 | Editor run configuration, extension prerequisites | `.vscode/tasks.json`, `launch.json` | JSON validated | `config/editor/README.md` |
| A7 | CMake source build instructions | `CMakeLists.txt`, toolchain file | Linux + cross builds | `README.md` |
| B1 | ASCII STL import, rotate/pan/zoom/fit | `stl_parser.cpp`, `geometry.js` | 11 STL tests, walkthrough 2, 3 | `tests/test_stl_parser.cpp` |
| B2 | Nodal CSV point cloud, legend, units, inspection | `nodal_csv.cpp`, `geometry.js` | 8 CSV tests, walkthrough 6 | `tests/test_nodal_csv.cpp` |
| B3 | Example files, provenance, schema, synthetic label | `workbench_cli samples` | file header comment, UI warning | `docs/methodology/DATA_PROVENANCE.md` |
| B4 | Reject malformed/non finite; explain empty; size limits | parsers, HTTP limits | tests, `file_size_limit_tests.csv` | `results/csv/` |
| C1 | 1D FEA, element K/F, assembly, both BCs | `rod_fea.hpp/.cpp` | hand computed 2 element test, dense cross check | `tests/test_thermal_fea.cpp` |
| C2 | N = 10, 20, 40, 80 vs exact, plots with units, CSV | CLI `fea`, Thermal view | `fea_*.csv`, plots | `results/` |
| C3 | Midpoint error with FE interpolation; convergence | `evaluateMidpointError` | order 2.000, = q h²/8k | `results/csv/fea_convergence.csv` |
| C4 | Assumptions, units, BCs, debugging insight | doc | | `docs/methodology/THERMAL_FEA.md` |
| D1 | Serial + parallel, bounded pool, independent solves, input order | `batch.cpp`, `thread_pool.cpp` | order and agreement tests | `tests/test_performance.cpp` |
| D2 | Reproducible generator, fixed seed, varied positive k, q | `generateBatch` | pinned first draw test | same |
| D3 | 1 to 4 workers, warm-up, 5 reps, median, S, E, host info | `runBenchmark`, `querySystemInfo` | Windows i5-14450HX and cloud runs | `results/benchmarks/` |
| D4 | Agreement ≤ 1e-8 °C; launch-to-ready separate | `maxAbsoluteDifference`, `millisecondsSinceProcessStart` | observed 0 °C; 388/705 ms | `docs/methodology/PERFORMANCE.md` |
| E1 | Block diagram, interface/pin/voltage table, JTAG explanation | doc + SVG | | `docs/hardware/` |
| E2 | Mocks: discovery, capabilities, connect/disconnect, telemetry, recoverable failure, labelled simulated | `hardware/*.cpp`, Hardware view | 6 hardware tests, walkthrough 13 to 15 | `tests/test_hardware.cpp` |
| F1 | Handwritten derivation (max 2 pages) | **candidate's own handwriting required** | reference sheet provided | `docs/methodology/backprop_derivation_sheet.pdf` |
| F2 | C++ backprop, no autodiff, central FD check, loss over 100 steps | `ml/backprop.cpp`, CLI, view | 6 tests, `backprop_*.csv`, plot | `results/` |
| S1 | ZIP layout, README, report ≤ 6 pages, walkthrough, git history, AI log, ownership | this package | | root |
