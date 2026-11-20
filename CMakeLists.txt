cmake_minimum_required(VERSION 3.20)
project(UnifiedEngineeringWorkbench VERSION 1.0.0 LANGUAGES CXX)

# ----------------------------------------------------------------- options
option(WB_BUILD_TESTS "Build the doctest unit/numerical test suite" ON)
option(WB_BUILD_APP   "Build the Launch executable (HTTP backend + embedded UI)" ON)
option(WB_BUILD_CLI   "Build workbench_cli (headless result/benchmark generator)" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

# Record the exact optimisation flags in the binary for benchmark reports.
string(TOUPPER "${CMAKE_BUILD_TYPE}" WB_BUILD_TYPE_UPPER)
set(WB_OPT_FLAGS_STRING "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${WB_BUILD_TYPE_UPPER}}")
string(STRIP "${WB_OPT_FLAGS_STRING}" WB_OPT_FLAGS_STRING)

# Strip the build machine's absolute source path from __FILE__ strings (assert and
# test messages) so binaries contain only repository-relative paths.
if(NOT MSVC)
  add_compile_options(-ffile-prefix-map=${CMAKE_SOURCE_DIR}/=)
endif()

if(MSVC)
  set(WB_WARNINGS /W4 /permissive-)
else()
  set(WB_WARNINGS -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

# Fully static runtime on MinGW so Launch.exe needs no libstdc++/winpthread DLLs.
if(MINGW)
  set(WB_STATIC_LINK -static -static-libgcc -static-libstdc++)
endif()

# -------------------------------------------------------------- C++ core
# Pure computation / file processing. No HTTP, no UI: reusable by the app,
# the CLI and the tests.
add_library(workbench_core STATIC
  src/utilities/text.cpp
  src/core/linear_algebra.cpp
  src/geometry/stl_parser.cpp
  src/thermal/nodal_csv.cpp
  src/thermal/rod_fea.cpp
  src/performance/thread_pool.cpp
  src/performance/batch.cpp
  src/performance/system_info.cpp
  src/hardware/device_manager.cpp
  src/hardware/mock_devices.cpp
  src/ml/backprop.cpp
)
target_include_directories(workbench_core PUBLIC include)
target_compile_options(workbench_core PRIVATE ${WB_WARNINGS})
target_compile_definitions(workbench_core PRIVATE
  WB_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  WB_OPT_FLAGS="${WB_OPT_FLAGS_STRING}")
target_link_libraries(workbench_core PUBLIC Threads::Threads)

# Third-party single-header libraries (vendored, versions in dependencies/VERSIONS.md).
add_library(workbench_thirdparty INTERFACE)
target_include_directories(workbench_thirdparty SYSTEM INTERFACE dependencies)

# --------------------------------------------- serialization (core -> JSON)
add_library(workbench_json STATIC src/server/json_mapping.cpp src/utilities/csv_export.cpp)
target_include_directories(workbench_json PUBLIC src)
target_link_libraries(workbench_json PUBLIC workbench_core workbench_thirdparty)
target_compile_options(workbench_json PRIVATE ${WB_WARNINGS})

# ------------------------------------------------------- application (Launch)
if(WB_BUILD_APP)
  # Embed every file under src/ui into the executable so Launch.exe is
  # self-contained (no loose web folder to lose when copying).
  file(GLOB_RECURSE WB_UI_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/ui/*)
  set(WB_EMBED_CPP ${CMAKE_BINARY_DIR}/generated/embedded_assets.cpp)
  add_custom_command(
    OUTPUT ${WB_EMBED_CPP}
    COMMAND ${CMAKE_COMMAND} -DUI_DIR=${CMAKE_SOURCE_DIR}/src/ui -DOUT=${WB_EMBED_CPP}
            -P ${CMAKE_SOURCE_DIR}/tools/embed_assets.cmake
    DEPENDS ${WB_UI_FILES} ${CMAKE_SOURCE_DIR}/tools/embed_assets.cmake
    COMMENT "Embedding web UI assets")

  add_executable(Launch
    src/app/main.cpp
    src/server/http_server.cpp
    src/server/job_manager.cpp
    src/server/browser_launcher.cpp
    ${WB_EMBED_CPP})
  if(WIN32)
    enable_language(RC)
    target_sources(Launch PRIVATE src/app/app.rc)   # icon + version info
  endif()
  target_link_libraries(Launch PRIVATE workbench_json ${WB_STATIC_LINK})
  target_compile_options(Launch PRIVATE ${WB_WARNINGS})
  if(WIN32)
    target_compile_definitions(Launch PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(Launch PRIVATE ws2_32 shell32)
  endif()
endif()

# ------------------------------------------------------------- CLI generator
if(WB_BUILD_CLI)
  add_executable(workbench_cli src/app/cli_main.cpp)
  target_link_libraries(workbench_cli PRIVATE workbench_json ${WB_STATIC_LINK})
  target_compile_options(workbench_cli PRIVATE ${WB_WARNINGS})
endif()

# --------------------------------------------------------------------- tests
if(WB_BUILD_TESTS)
  enable_testing()
  add_executable(workbench_tests
    tests/test_main.cpp
    tests/test_text_utils.cpp
    tests/test_stl_parser.cpp
    tests/test_nodal_csv.cpp
    tests/test_thermal_fea.cpp
    tests/test_performance.cpp
    tests/test_hardware.cpp
    tests/test_backprop.cpp
    tests/test_json_mapping.cpp)
  target_link_libraries(workbench_tests PRIVATE workbench_json ${WB_STATIC_LINK})
  add_test(NAME workbench_tests COMMAND workbench_tests --reporters=console)
endif()
