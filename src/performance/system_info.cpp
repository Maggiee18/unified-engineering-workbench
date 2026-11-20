#include "workbench/performance/system_info.hpp"

#include "workbench/performance/thread_pool.hpp"

#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <utility>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <vector>
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

#ifndef WB_BUILD_TYPE
#define WB_BUILD_TYPE "unknown"
#endif
#ifndef WB_OPT_FLAGS
#define WB_OPT_FLAGS "unknown"
#endif

namespace wb::perf {
namespace {

std::string cpuBrandString() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int regs[4] = {0, 0, 0, 0};
    if (__get_cpuid(0x80000000u, &regs[0], &regs[1], &regs[2], &regs[3]) && regs[0] >= 0x80000004u) {
        char brand[49] = {};
        for (unsigned int leaf = 0; leaf < 3; ++leaf) {
            __get_cpuid(0x80000002u + leaf, &regs[0], &regs[1], &regs[2], &regs[3]);
            std::memcpy(brand + leaf * 16, regs, 16);
        }
        std::string s(brand);
        const auto first = s.find_first_not_of(' ');
        return first == std::string::npos ? "unknown x86 CPU" : s.substr(first);
    }
#endif
    return "unknown CPU";
}

std::string compilerString() {
#if defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__MINGW64__)
    return std::string("MinGW-w64 GCC ") + __VERSION__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown compiler";
#endif
}

#ifdef _WIN32
std::size_t physicalCoreCount() {
    DWORD length = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &length);
    if (length == 0) return 0;
    std::vector<unsigned char> buffer(length);
    auto* info = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
    if (!GetLogicalProcessorInformationEx(RelationProcessorCore, info, &length)) return 0;
    std::size_t cores = 0;
    for (DWORD offset = 0; offset < length;) {
        auto* item = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
        if (item->Relationship == RelationProcessorCore) ++cores;
        offset += item->Size;
    }
    return cores;
}

std::uint64_t totalRam() {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    return GlobalMemoryStatusEx(&status) ? status.ullTotalPhys : 0;
}

std::string osName() {
    // RtlGetVersion is not affected by the compatibility shims of GetVersionEx.
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        RTL_OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        if (fn != nullptr && fn(&v) == 0) {
            const char* family = (v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000) ? "Windows 11" : "Windows";
            return std::string(family) + " (build " + std::to_string(v.dwBuildNumber) + ")";
        }
    }
    return "Windows";
}
#else
std::size_t physicalCoreCount() {
    std::ifstream in("/proc/cpuinfo");
    std::set<std::pair<std::string, std::string>> cores;
    std::string line, physicalId = "0";
    while (std::getline(in, line)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        const std::string key = line.substr(0, line.find_last_not_of(" \t", colon - 1) + 1);
        const std::string value = colon + 2 <= line.size() ? line.substr(colon + 2) : "";
        if (key == "physical id") physicalId = value;
        if (key == "core id") cores.insert({physicalId, value});
    }
    return cores.size();
}

std::uint64_t totalRam() {
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    return (pages > 0 && pageSize > 0) ? static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize) : 0;
}

std::string osName() {
    utsname u{};
    if (uname(&u) == 0) return std::string(u.sysname) + " " + u.release;
    return "POSIX";
}
#endif

}  // namespace

SystemInfo querySystemInfo() {
    SystemInfo info;
    info.cpuModel = cpuBrandString();
    info.physicalCores = physicalCoreCount();
    info.logicalCpus = logicalCpuCount();
    info.totalRamBytes = totalRam();
    info.operatingSystem = osName();
    info.compiler = compilerString();
    info.buildType = WB_BUILD_TYPE;
    info.optimisationFlags = WB_OPT_FLAGS;
    return info;
}

}  // namespace wb::perf
