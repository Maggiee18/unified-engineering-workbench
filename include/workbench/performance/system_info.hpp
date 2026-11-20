// Host description recorded alongside every benchmark.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace wb::perf {

struct SystemInfo {
    std::string cpuModel;
    std::size_t physicalCores = 0;  // 0 = could not be determined
    std::size_t logicalCpus = 0;
    std::uint64_t totalRamBytes = 0;
    std::string operatingSystem;
    std::string compiler;
    std::string buildType;
    std::string optimisationFlags;
};

SystemInfo querySystemInfo();

}  // namespace wb::perf
