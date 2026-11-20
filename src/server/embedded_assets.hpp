// Web UI files compiled into the executable by tools/embed_assets.cmake.
#pragma once

#include <cstddef>
#include <string_view>

namespace wb::api {

struct EmbeddedAsset {
    std::string_view path;  // e.g. "index.html", "vendor/chart.umd.js"
    const unsigned char* data;
    std::size_t size;
};

/// Returns nullptr when no asset with that relative path was embedded.
const EmbeddedAsset* findEmbeddedAsset(std::string_view path) noexcept;

}  // namespace wb::api
