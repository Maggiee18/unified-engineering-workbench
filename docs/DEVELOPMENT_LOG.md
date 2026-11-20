# Development log (concise)

All times UTC, 2026-11-01/02, single Claude Cowork session (see AI_USE_LOG.md). The
git history (`docs/COMMIT_LOG.txt`, `.git`, `docs/workbench.bundle`) is the
authoritative record; this file adds the reasoning between commits.

| Time | Phase | What happened |
|---|---|---|
| 17:23 | Audit | Read the 3 page brief from the candidate's Downloads folder; built the requirements matrix; checked toolchains (GCC 13, CMake 3.28, no Windows compiler, no Qt). |
| 17:25 | Decision | Installed MinGW-w64 (POSIX threads) for cross compiling; chose static exe + embedded web UI over Qt 6 for launch gate safety. Vendored pinned deps with checksums. |
| 17:28 to 17:38 | Core | STL and CSV parsers, banded FEA solver, thread pool and benchmark, mocks, backprop, CLI, 62 tests. First test run: 1 failure (test bug: truncated input mid line). Commit 1. |
| 17:39 | Validation | FEA errors 1.25/0.3125/0.078125/0.01953 °C, order 2.000; gradient check pass; numbers re-checked in Python. Corrected a wrong hand-rounded gradient in a test. |
| 17:40 to 17:50 | Transport + UI | HTTP server, job manager, embedded assets, five views. Playwright walkthrough found the Performance button bug (form field named `elements`). Commit 2. |
| 17:51 | Packaging | Windows cross build: only system DLLs imported; Windows tests 62/62 under Wine; full walkthrough against the Windows exe under Wine. |
| 17:52 | Benchmark | Cloud VM benchmark (2 vCPU): S = 1.96 at p = 2; p = 3, 4 skipped. |
| 18:03 | Target PC | Preview ZIP extracted on the candidate's laptop; Launch.exe double clicked in Explorer: UI opened (388 ms / 705 ms). RunTests.cmd 62/62 and RunBenchmark.cmd (S = 3.63 at p = 4) run natively. |
| 18:10 | Robustness | Size limit tests with curl found an 8 KiB cap on form urlencoded uploads and an empty 413 body; fixed. Responsive layout fix after seeing the narrow Edge window on the laptop. |
| 18:15 to 18:45 | Docs | Hardware design + SVG, FEA, performance, provenance, testing, architecture/traceability, README, AI log, ownership, report (5 pages), backprop reference sheet (two values corrected after an independent check). |
| 18:45 onward | Release | Final Windows build, walkthrough, package, native re-test of the final ZIP. |
