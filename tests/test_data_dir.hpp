// Locates the example data folder for tests without machine-specific paths:
// env WB_TEST_DATA_DIR, then ./data, ../data, ../../data (packaged bin/ or
// build/ trees), then the source-tree path baked in by CMake as a last resort.
#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>

inline std::string testDataDir() {
    if (const char* env = std::getenv("WB_TEST_DATA_DIR")) return env;
    for (const char* candidate : {"data", "../data", "../../data"}) {
        std::error_code ec;
        if (std::filesystem::is_directory(std::filesystem::path(candidate) / "geometry", ec)) return candidate;
    }
#ifdef WB_TEST_DATA_DIR
    return WB_TEST_DATA_DIR;
#else
    return "data";
#endif
}
