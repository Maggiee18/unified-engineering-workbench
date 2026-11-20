@echo off
rem Headless multicore benchmark (same C++ code as the Performance view).
rem 2000 cases x 2000 elements, workers 1-4 (capped at logical CPUs), 1 warm-up + 5 runs.
rem Writes CSV + JSON into ..\results\benchmarks\this_pc\
cd /d "%~dp0"
if not exist "..\results\benchmarks\this_pc" mkdir "..\results\benchmarks\this_pc"
workbench_cli.exe benchmark "..\results" 2000 2000 this_pc > "..\results\benchmarks\this_pc\console_log.txt" 2>&1
type "..\results\benchmarks\this_pc\console_log.txt"
echo.
echo Results written to results\benchmarks\this_pc\
pause
