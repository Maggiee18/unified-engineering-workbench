# Timestamped screenshot walkthrough with logs

This is the screenshot alternative to the 3 to 5 minute video allowed by the brief.

* **Native Windows 11 (target laptop):** `windows_native/01_launch_exe_double_click_2026-11-01_2334.jpg` shows the first double click of `Launch.exe` from File Explorer in the extracted package (2026-11-01 23:34 IST). The packaged tests and benchmark were also run there (`results/test_logs/`, `results/benchmarks/windows11_i5-14450HX/`).
* **Full walkthrough below:** the **same Windows `Launch.exe`** run under Wine 9.0 in the Linux build sandbox, driven by `tools/ui_walkthrough.mjs` (Playwright, headless Chromium, software WebGL). Times are UTC. The browser console log had 4 errors; the in-app log is in `ui_log.txt` and the backend console log in `backend_log.txt`.

| # | Time (UTC) | What is shown | Status bar | Screenshot |
|---|---|---|---|---|
| 1 | 18:36:56 | Application opened; backend online indicator and launch-to-ready times | Backprop: J0=1.18254, gradient check PASS, J after 100 steps = 1.703e-20 | [01_launch.png](01_launch.png) |
| 2 | 18:36:57 | Example ASCII STL parsed in C++ and rendered (fit-to-view) | STL loaded: heatsink_bracket.stl - 224 triangles in 1.08 ms | [02_stl_example.png](02_stl_example.png) |
| 3 | 18:37:01 | Rotated (left-drag) and zoomed (wheel) with wireframe overlay | STL loaded: heatsink_bracket.stl - 224 triangles in 1.08 ms | [03_stl_rotate_zoom.png](03_stl_rotate_zoom.png) |
| 4 | 18:37:03 | Malformed STL (NaN vertex) rejected with line number; previous model kept | STL rejected: line 4: vertex component 'nan' is not finite (NaN/Inf are rejected) | [04_stl_malformed.png](04_stl_malformed.png) |
| 5 | 18:37:04 | Empty STL file explained | STL rejected: the STL file is empty (0 bytes) | [05_stl_empty.png](05_stl_empty.png) |
| 6 | 18:37:06 | Synthetic nodal-temperature CSV as coloured point cloud with °C legend and node inspection | Node 2113: 38.866 °C | [06_csv_point_cloud.png](06_csv_point_cloud.png) |
| 7 | 18:37:07 | CSV with missing z_m column rejected with actionable message | CSV rejected: line 1: header is missing required column(s): z_m (expected node_id,x_m,y_m,z_m,temperature_C) | [07_csv_missing_column.png](07_csv_missing_column.png) |
| 8 | 18:37:07 | FEA for N = 10, 20, 40, 80 vs exact solution; midpoint errors and log-log convergence | CSV rejected: line 1: header is missing required column(s): z_m (expected node_id,x_m,y_m,z_m,temperature_C) | [08_thermal_convergence.png](08_thermal_convergence.png) |
| 9 | 18:37:07 | Invalid conductivity flagged before any solve | Some parameters are invalid - fix the highlighted fields (hover for details). | [09_thermal_validation.png](09_thermal_validation.png) |
| 10 | 18:37:08 | Benchmark running in a C++ background job; UI stays responsive with progress | Benchmark started (job-1): 400 cases × 2000 elements, workers 1,2 | [10_performance_running.png](10_performance_running.png) |
| 11 | 18:37:09 | Serial vs thread-pool medians, speedup, efficiency and 1e-8 °C agreement check | Benchmark finished in 0.705 s; agreement PASS | [11_performance_results.png](11_performance_results.png) |
| 12 | 18:37:10 | Cancellation: job stopped safely, earlier results untouched | Benchmark cancelled safely | [12_performance_cancelled.png](12_performance_cancelled.png) |
| 13 | 18:37:11 | All three simulated devices discovered, connected, telemetry received | jtag0: telemetry #1 (vtref=3.2931 V, tck_frequency=4000 kHz, target_idcode=0x4BA00477, scan_chain_devices=1) | [13_hardware_connected.png](13_hardware_connected.png) |
| 14 | 18:37:11 | Injected telemetry timeout: FPGA faulted with recoverable error | fpga0: telemetry failed - fpga0: telemetry request timed out after 250 ms (simulated fault) (recoverable: use Recover) | [14_hardware_timeout.png](14_hardware_timeout.png) |
| 15 | 18:37:12 | Recovery reconnects the device and telemetry resumes | fpga0: telemetry #1 (xadc_temperature=41.134 C, vccint=0.99429 V, vccaux=1.8037 V, mmcm_locked=true, design_id=0x00C0FFEE) | [15_hardware_recovered.png](15_hardware_recovered.png) |
| 16 | 18:37:12 | Hand-coded gradients vs central differences (ε = 1e-6), one update, loss over 100 steps | fpga0: telemetry #1 (xadc_temperature=41.134 C, vccint=0.99429 V, vccaux=1.8037 V, mmcm_locked=true, design_id=0x00C0FFEE) | [16_backprop.png](16_backprop.png) |
| 17 | 18:37:13 | Application log panel | fpga0: telemetry #1 (xadc_temperature=41.134 C, vccint=0.99429 V, vccaux=1.8037 V, mmcm_locked=true, design_id=0x00C0FFEE) | [17_log.png](17_log.png) |

Suggested live demo order (about 4 minutes): steps 1, 2 to 3, 4, 6, 8, 9, 10 to 11, 12, 13 to 15, 16.
