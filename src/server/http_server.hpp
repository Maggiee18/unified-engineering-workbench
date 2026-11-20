// Local HTTP transport between the browser UI and the C++ core.
//
// Binds to 127.0.0.1 only (never reachable from the network). Every endpoint
// is a thin adapter: parse JSON -> call core -> serialize result. All error
// types are mapped to HTTP status codes with an actionable JSON message.
#pragma once

#include "server/job_manager.hpp"

#include "workbench/hardware/device.hpp"

#include <atomic>
#include <memory>
#include <string>

namespace httplib {
class Server;
}

namespace wb::api {

struct ServerOptions {
    std::string host = "127.0.0.1";
    int preferredPort = 8765;   // first port tried; next free one is used otherwise
    std::string dataDir;        // e.g. <exe dir>/data (example files)
    double processStartToMainMs = 0.0;
    std::string launchLogPath;  // <exe dir>/logs/launch.log; empty = no file log
};

/// Appends one timestamped line to the launch log (best effort, never throws).
void appendLaunchLog(const std::string& path, const std::string& line);

class WorkbenchServer {
public:
    explicit WorkbenchServer(ServerOptions options);
    ~WorkbenchServer();

    /// Binds a free port; returns the port or -1 on failure.
    int bind();
    /// Blocks serving requests until stop() or POST /api/shutdown.
    void run();
    void stop();
    /// Milliseconds from process creation until the socket was listening.
    void setBackendReadyMs(double ms) { backendReadyMs_ = ms; }

private:
    void registerRoutes();

    ServerOptions options_;
    std::unique_ptr<httplib::Server> http_;
    hw::DeviceManager devices_;
    JobManager jobs_;
    int port_ = -1;
    double backendReadyMs_ = 0.0;
    std::atomic<double> uiReadyMs_{0.0};
};

/// Milliseconds since the OS created this process (Windows: GetProcessTimes).
double millisecondsSinceProcessStart();

}  // namespace wb::api
