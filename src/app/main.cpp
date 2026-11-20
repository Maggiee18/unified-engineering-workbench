// Launch.exe entry point.
//
// Starts the C++ backend on 127.0.0.1, then opens the embedded web UI.
// The console window doubles as the application log; closing it (or the
// "Quit" button in the UI) stops the workbench.
//
// Options:  --no-browser   start the backend only (tests, headless use)
//           --port <n>     first port to try (default 8765)
#include "server/browser_launcher.hpp"
#include "server/http_server.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

/// Directory containing the executable, so data/ resolves relative to
/// Launch.exe no matter what the working directory is (double-click,
/// shortcut, VS Code task).
std::filesystem::path executableDirectory(const char* argv0) {
#ifdef _WIN32
    (void)argv0;
    std::wstring buffer(32768, L'\0');
    const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
#else
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) return self.parent_path();
    return std::filesystem::absolute(argv0).parent_path();
#endif
}

/// Data folder next to the exe; falls back to ../data and ../../data so a
/// developer build inside build/ also finds the repository's data folder.
std::string locateDataDir(const std::filesystem::path& exeDir) {
    for (const auto& candidate : {exeDir / "data", exeDir.parent_path() / "data", exeDir.parent_path().parent_path() / "data"}) {
        std::error_code ec;
        if (std::filesystem::is_directory(candidate / "geometry", ec)) return candidate.u8string();
    }
    return (exeDir / "data").u8string();
}

}  // namespace

int main(int argc, char** argv) {
    const double startMs = wb::api::millisecondsSinceProcessStart();
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleTitleW(L"Unified Engineering Workbench - backend log (close to exit)");
#endif
    bool openBrowser = true;
    int port = 8765;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--no-browser") {
            openBrowser = false;
        } else if (arg == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        }
    }

    const auto exeDir = executableDirectory(argv[0]);
    wb::api::ServerOptions options;
    options.preferredPort = port;
    options.dataDir = locateDataDir(exeDir);
    options.processStartToMainMs = startMs;
    {
        std::error_code ec;
        std::filesystem::create_directories(exeDir / "logs", ec);
        if (!ec) options.launchLogPath = (exeDir / "logs" / "launch.log").u8string();
    }

    std::cout << "Unified Engineering Workbench 1.0.0\n"
              << "  C++17 backend, local-only HTTP transport (127.0.0.1)\n"
              << "  data folder: " << options.dataDir << '\n';

    try {
        wb::api::WorkbenchServer server(options);
        const int bound = server.bind();
        if (bound < 0) {
            std::cerr << "ERROR: no free local port in range " << port << "-" << port + 99 << '\n';
#ifdef _WIN32
            MessageBoxW(nullptr, L"The workbench could not open a local port (8765-8864 are all busy).",
                        L"Unified Engineering Workbench", MB_ICONERROR);
#endif
            return 1;
        }
        const double readyMs = wb::api::millisecondsSinceProcessStart();
        server.setBackendReadyMs(readyMs);
        const std::string url = "http://127.0.0.1:" + std::to_string(bound) + "/";
        std::cout << "  backend ready in " << static_cast<int>(readyMs) << " ms (process start -> listening)\n"
                  << "  UI: " << url << "\n";
        std::string browser = "browser not opened (--no-browser)";
        // A unique query string makes Edge open a fresh app window for this run
        // instead of re-focusing a stale window left over from an earlier run.
        const auto launchId = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch()).count();
        if (openBrowser) browser = wb::api::openUserInterface(url + "?launch=" + std::to_string(launchId));
        std::cout << "  " << browser << '\n';
        wb::api::appendLaunchLog(options.launchLogPath, "backend ready " + std::to_string(static_cast<int>(readyMs)) +
                                                            " ms after process start; " + url + "; " + browser);
        std::cout << "Close this window or press Quit in the UI to stop the workbench.\n" << std::endl;
        server.run();
        std::cout << "Workbench stopped." << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << '\n';
#ifdef _WIN32
        MessageBoxA(nullptr, e.what(), "Unified Engineering Workbench - fatal error", MB_ICONERROR);
#endif
        return 1;
    }
    return 0;
}
