// Nodal temperature CSV import (ANSYS-style export layout).
//
// Required header columns (any order, case-insensitive, extra columns ignored):
//   node_id,x_m,y_m,z_m,temperature_C
// Units: coordinates in metres, temperature in degrees Celsius.
// Lines starting with '#' are treated as comments (used to label synthetic data).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wb::thermal {

struct NodalTemperature {
    std::int64_t nodeId = 0;
    double x = 0.0;  // m
    double y = 0.0;  // m
    double z = 0.0;  // m
    double temperatureC = 0.0;
};

struct NodalDataset {
    std::vector<NodalTemperature> nodes;
    double minTemperatureC = 0.0;
    double maxTemperatureC = 0.0;
    double minCoord[3] = {0, 0, 0};
    double maxCoord[3] = {0, 0, 0};
    std::vector<std::string> comments;   // '#' lines, e.g. a SYNTHETIC label
    std::vector<std::string> warnings;
};

struct NodalCsvLimits {
    std::uint64_t maxBytes = 64ull * 1024 * 1024;
    std::size_t maxNodes = 1'000'000;
};

/// Physical lower bound used to reject impossible temperatures.
inline constexpr double kAbsoluteZeroC = -273.15;

/// Parses CSV text. Throws wb::ParseError (with line number) on bad header,
/// wrong field count, non-numeric or non-finite values, duplicate node ids,
/// temperatures below absolute zero, empty files and limit violations.
NodalDataset parseNodalTemperatureCsv(std::string_view text, const NodalCsvLimits& limits = {});

NodalDataset loadNodalTemperatureCsvFile(const std::string& path, const NodalCsvLimits& limits = {});

}  // namespace wb::thermal
