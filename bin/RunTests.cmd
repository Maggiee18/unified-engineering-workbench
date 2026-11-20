@echo off
rem Runs the packaged C++ test suite (62 test cases) and keeps the window open.
cd /d "%~dp0"
workbench_tests.exe
echo.
echo Test log also saved to %~dp0test_output.txt
workbench_tests.exe > test_output.txt 2>&1
pause
