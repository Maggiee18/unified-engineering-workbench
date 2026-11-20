# Third-party dependencies (lock file)

All dependencies are vendored in this repository; nothing is downloaded at build or
run time. Licences are reproduced next to each file.

| Component | Version | Files | Licence | Source | SHA-256 |
|---|---|---|---|---|---|
| cpp-httplib | 0.18.3 | `dependencies/httplib.h` | MIT (`LICENSE.cpp-httplib.txt`) | github.com/yhirose/cpp-httplib tag v0.18.3 | a0a0c13dc086663863dbe6730a19716f7d3744904b6205496e8301750814dbd5 |
| nlohmann/json | 3.11.3 | `dependencies/json.hpp` | MIT (`LICENSE.nlohmann-json.txt`) | github.com/nlohmann/json release v3.11.3 | 9bea4c8066ef4a1c206b2be5a36302f8926f7fdc6087af5d20b417d0cf103ea6 |
| doctest | 2.4.11 | `dependencies/doctest.h` (tests only) | MIT (`LICENSE.doctest.txt`) | github.com/doctest/doctest tag v2.4.11 | 44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb |
| three.js | 0.170.0 | `src/ui/vendor/three.module.min.js`, `OrbitControls.js` | MIT (`LICENSE.three.txt`) | npm `three@0.170.0` | 08fd7545d13d2c7fb65ab691530a802dafefd638596501854f267d0fb13c39e7 / 80efaadea4f8a636a65fb0bd08bfef62f3d93a0bb94e2e7500f23176c5c07f4e |
| Chart.js | 4.4.7 | `src/ui/vendor/chart.umd.js` | MIT (`LICENSE.chartjs.md`) | npm `chart.js@4.4.7` | 2812cb8825fdc57469eb2f7bb055e9429244e599920511ee477e828499b632cb |

Toolchain used for the submitted binaries (not redistributed except the statically
linked runtime):

| Tool | Version | Notes |
|---|---|---|
| MinGW-w64 GCC (POSIX threads) | 13 (Ubuntu 24.04 package `g++-mingw-w64-x86-64-posix` 13.2.0) | `-O3 -DNDEBUG`, `-static -static-libgcc -static-libstdc++`; libstdc++/libgcc/winpthreads are under GPL with the GCC Runtime Library Exception / MIT-style licences that allow static linking into applications |
| CMake | 3.28.3 | |
| Ninja | 1.11.1 | |
| GCC (Linux test build) | 13.3.0 | |

Development-only tools (not shipped in the application): Node 22 + Playwright
(UI walkthrough), Python 3 + matplotlib (report plots from the C++ CSVs), Wine 9.0
(running the Windows test binary in the Linux sandbox).
