// ASCII STL import: data model and parser interface.
//
// Supported grammar (case-insensitive keywords, any whitespace layout):
//   solid [name]
//     facet normal nx ny nz
//       outer loop
//         vertex x y z      (exactly three per loop)
//       endloop
//     endfacet
//   endsolid [name]
// Binary STL, native CAD solids and multi-solid files are out of scope.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace wb::geometry {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Triangle {
    Vec3 normal;                // as stored in file, or recomputed if zero
    std::array<Vec3, 3> vertex;
};

struct BoundingBox {
    Vec3 min;
    Vec3 max;
    Vec3 centre() const noexcept;
    double diagonal() const noexcept;
};

struct StlMesh {
    std::string solidName;
    std::vector<Triangle> triangles;
    BoundingBox bounds;
    std::size_t degenerateTriangles = 0;   // zero-area facets (kept, but reported)
    std::size_t recomputedNormals = 0;     // facets whose stored normal was zero
    double surfaceArea = 0.0;              // in file units squared
    std::vector<std::string> warnings;
};

struct StlLimits {
    std::uint64_t maxBytes = 64ull * 1024 * 1024;   // 64 MiB of text
    std::size_t maxTriangles = 1'000'000;
};

/// Parses ASCII STL text. Throws wb::ParseError (with line number) for any
/// malformed construct, non-finite coordinate, empty file or binary STL.
StlMesh parseAsciiStl(std::string_view text, const StlLimits& limits = {});

/// Reads the file (enforcing limits.maxBytes) and parses it.
StlMesh loadAsciiStlFile(const std::string& path, const StlLimits& limits = {});

}  // namespace wb::geometry
