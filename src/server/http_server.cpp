#include "server/http_server.hpp"

#include "server/embedded_assets.hpp"
#include "utilities/csv_export.hpp"

#include "workbench/core/errors.hpp"
#include "workbench/geometry/stl_mesh.hpp"
#include "workbench/ml/backprop.hpp"
#include "workbench/performance/batch.hpp"
#include "workbench/performance/system_info.hpp"
#include "workbench/thermal/nodal_csv.hpp"
#include "workbench/thermal/rod_fea.hpp"

// Accept large uploads whatever Content-Type the client sends (httplib's default
// cap for form-urlencoded bodies is 8 KiB, found while testing with curl).
#define CPPHTTPLIB_FORM_URL_ENCODED_PAYLOAD_MAX_LENGTH (64ull * 1024 * 1024)
#include <httplib.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

namespace wb::api {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxUploadBytes = 64ull * 1024 * 1024;

void sendJson(httplib::Response& res, const Json& body, int status = 200) {
    res.status = status;
    // error_handler::replace keeps NaN-free, UTF-8-safe output for any file name.
    res.set_content(body.dump(-1, ' ', false, Json::error_handler_t::replace), "application/json");
}

void sendError(httplib::Response& res, int status, const std::string& kind, const std::string& message,
               Json extra = Json::object()) {
    extra["error"] = message;
    extra["kind"] = kind;
    sendJson(res, extra, status);
}

Json parseBody(const httplib::Request& req) {
    if (req.body.empty()) return Json::object();
    try {
        return Json::parse(req.body);
    } catch (const Json::parse_error&) {
        throw ValidationError("request body is not valid JSON");
    }
}

/// Wraps a handler so every failure becomes a structured JSON error.
template <typename Fn>
httplib::Server::Handler guarded(Fn fn) {
    return [fn](const httplib::Request& req, httplib::Response& res) {
        const auto t0 = Clock::now();
        try {
            fn(req, res);
        } catch (const ParseError& e) {
            sendError(res, 422, "parse_error", e.what(), {{"line", e.line()}});
        } catch (const ValidationError& e) {
            sendError(res, 400, "validation_error", e.what());
        } catch (const hw::DeviceTimeoutError& e) {
            sendError(res, 504, "device_timeout", e.what(), {{"recoverable", true}});
        } catch (const hw::DeviceStateError& e) {
            sendError(res, 409, "device_state", e.what());
        } catch (const NumericalError& e) {
            sendError(res, 500, "numerical_error", e.what());
        } catch (const std::exception& e) {
            sendError(res, 500, "internal_error", e.what());
        }
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        if (req.path.rfind("/api/", 0) == 0 && req.path != "/api/jobs" && req.path.find("/api/jobs/") != 0) {
            std::cout << "[api] " << req.method << ' ' << req.path << " -> " << res.status << " (" << static_cast<int>(ms)
                      << " ms)" << std::endl;
        }
    };
}

const char* mimeType(const std::string& path) {
    auto ends = [&](const char* ext) {
        const std::string e(ext);
        return path.size() >= e.size() && path.compare(path.size() - e.size(), e.size(), e) == 0;
    };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".js")) return "text/javascript; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

std::vector<std::size_t> meshListFromJson(const Json& j) {
    std::vector<std::size_t> meshes = {10, 20, 40, 80};
    if (j.contains("meshes")) {
        if (!j.at("meshes").is_array() || j.at("meshes").empty()) {
            throw ValidationError("'meshes' must be a non-empty list of element counts");
        }
        meshes.clear();
        for (const auto& m : j.at("meshes")) {
            if (!m.is_number_integer() || m.get<long long>() < 1 || m.get<long long>() > 100000) {
                throw ValidationError("each mesh size must be a whole number between 1 and 100000");
            }
            meshes.push_back(static_cast<std::size_t>(m.get<long long>()));
        }
    }
    return meshes;
}

}  // namespace

void appendLaunchLog(const std::string& path, const std::string& line) {
    if (path.empty()) return;
    try {
        const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char stamp[32] = {};
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        std::ofstream out(std::filesystem::u8path(path), std::ios::app);
        if (out) out << stamp << "  " << line << '\n';
    } catch (...) {
        // Logging must never break the application (e.g. read-only folder).
    }
}

double millisecondsSinceProcessStart() {
#ifdef _WIN32
    FILETIME creation, exitTime, kernel, user, now;
    if (GetProcessTimes(GetCurrentProcess(), &creation, &exitTime, &kernel, &user)) {
        GetSystemTimeAsFileTime(&now);
        ULARGE_INTEGER c, n;
        c.LowPart = creation.dwLowDateTime;
        c.HighPart = creation.dwHighDateTime;
        n.LowPart = now.dwLowDateTime;
        n.HighPart = now.dwHighDateTime;
        return static_cast<double>(n.QuadPart - c.QuadPart) / 10'000.0;  // 100 ns ticks -> ms
    }
#endif
    static const auto firstCall = Clock::now();  // fallback: time since first call in main()
    return std::chrono::duration<double, std::milli>(Clock::now() - firstCall).count();
}

WorkbenchServer::WorkbenchServer(ServerOptions options)
    : options_(std::move(options)), http_(std::make_unique<httplib::Server>()) {
    http_->set_payload_max_length(kMaxUploadBytes);
    // Never share the port with another process. httplib's default sets
    // SO_REUSEADDR (Windows) / SO_REUSEPORT (Linux), which let a second
    // Launch.exe bind the SAME port as a running one, so requests were split
    // between two backends (found when launching twice on the target PC).
    http_->set_socket_options([](socket_t sock) {
        int on = 1;
#ifdef _WIN32
        setsockopt(sock, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&on), sizeof(on));
#else
        setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const void*>(&on), sizeof(on));
#endif
    });
    // Transport-level rejections (e.g. 413 body too large) also get a JSON message.
    http_->set_error_handler([](const httplib::Request&, httplib::Response& res) {
        if (!res.body.empty()) return;
        if (res.status == 413) {
            sendError(res, 413, "validation_error", "upload exceeds the 64 MiB limit");
        } else {
            sendError(res, res.status, "http_error", "HTTP error " + std::to_string(res.status));
        }
    });
    devices_.discover();
    registerRoutes();
}

WorkbenchServer::~WorkbenchServer() = default;

int WorkbenchServer::bind() {
    for (int port = options_.preferredPort; port < options_.preferredPort + 100; ++port) {
        if (http_->bind_to_port(options_.host, port)) {
            port_ = port;
            return port;
        }
        // httplib marks the server "decommissioned" after a failed bind; stop()
        // (a no-op while not running) clears that so the next port can be tried.
        http_->stop();
    }
    return -1;
}

void WorkbenchServer::run() { http_->listen_after_bind(); }
void WorkbenchServer::stop() { http_->stop(); }

void WorkbenchServer::registerRoutes() {
    auto& s = *http_;

    // --------------------------------------------------------------- system
    s.Get("/api/system", guarded([this](const httplib::Request&, httplib::Response& res) {
        Json j = toJson(perf::querySystemInfo());
        j["backendReadyMs"] = backendReadyMs_;
        j["uiReadyMs"] = uiReadyMs_.load();
        j["port"] = port_;
        j["version"] = "1.0.0";
        sendJson(res, j);
    }));
    s.Post("/api/ui-ready", guarded([this](const httplib::Request&, httplib::Response& res) {
        double expected = 0.0;
        const double now = millisecondsSinceProcessStart();
        if (uiReadyMs_.compare_exchange_strong(expected, now)) {
            std::cout << "[app] UI ready " << static_cast<int>(now) << " ms after process start" << std::endl;
            appendLaunchLog(options_.launchLogPath, "UI ready " + std::to_string(static_cast<int>(now)) +
                                                        " ms after process start");
        }
        sendJson(res, {{"uiReadyMs", uiReadyMs_.load()}, {"backendReadyMs", backendReadyMs_}});
    }));
    s.Post("/api/shutdown", guarded([this](const httplib::Request&, httplib::Response& res) {
        sendJson(res, {{"ok", true}});
        std::thread([this] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            stop();
        }).detach();
    }));

    // ------------------------------------------------------------- geometry
    s.Post("/api/geometry/stl", guarded([](const httplib::Request& req, httplib::Response& res) {
        const auto t0 = Clock::now();
        const geometry::StlMesh mesh = geometry::parseAsciiStl(req.body);
        Json j = toJson(mesh);
        j["parseMs"] = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        j["bytes"] = req.body.size();
        sendJson(res, j);
    }));
    s.Get("/api/geometry/example", guarded([this](const httplib::Request&, httplib::Response& res) {
        const std::string path = options_.dataDir + "/geometry/heatsink_bracket.stl";
        const auto t0 = Clock::now();
        const geometry::StlMesh mesh = geometry::loadAsciiStlFile(path);
        Json j = toJson(mesh);
        j["parseMs"] = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        j["fileName"] = "heatsink_bracket.stl";
        sendJson(res, j);
    }));
    s.Post("/api/results/nodal-csv", guarded([](const httplib::Request& req, httplib::Response& res) {
        const auto t0 = Clock::now();
        const thermal::NodalDataset data = thermal::parseNodalTemperatureCsv(req.body);
        Json j = toJson(data);
        j["parseMs"] = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        j["bytes"] = req.body.size();
        sendJson(res, j);
    }));
    s.Get("/api/results/example", guarded([this](const httplib::Request&, httplib::Response& res) {
        const std::string path = options_.dataDir + "/thermal/synthetic_nodal_temperatures.csv";
        const auto t0 = Clock::now();
        const thermal::NodalDataset data = thermal::loadNodalTemperatureCsvFile(path);
        Json j = toJson(data);
        j["parseMs"] = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        j["fileName"] = "synthetic_nodal_temperatures.csv";
        sendJson(res, j);
    }));

    // -------------------------------------------------------------- thermal
    s.Post("/api/thermal/solve", guarded([](const httplib::Request& req, httplib::Response& res) {
        const Json body = parseBody(req);
        const thermal::RodProblem p = rodProblemFromJson(body.value("problem", Json::object()));
        const std::size_t n = countOr(body, "elements", 10);
        const auto t0 = Clock::now();
        const thermal::RodSolution sol = thermal::solveRod(p, n);
        const double solveMs = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        Json j = toJson(sol, thermal::evaluateMidpointError(sol));
        j["solveMs"] = solveMs;
        sendJson(res, j);
    }));
    s.Post("/api/thermal/convergence", guarded([](const httplib::Request& req, httplib::Response& res) {
        const Json body = parseBody(req);
        const thermal::RodProblem p = rodProblemFromJson(body.value("problem", Json::object()));
        const auto meshes = meshListFromJson(body);
        const auto rows = thermal::runConvergenceStudy(p, meshes);
        Json solutions = Json::array();
        for (std::size_t n : meshes) {
            const auto sol = thermal::solveRod(p, n);
            solutions.push_back(toJson(sol, thermal::evaluateMidpointError(sol), 200));
        }
        sendJson(res, {{"rows", toJson(rows)}, {"solutions", solutions}, {"problem", toJson(p)}});
    }));
    s.Post("/api/thermal/export", guarded([](const httplib::Request& req, httplib::Response& res) {
        const Json body = parseBody(req);
        const thermal::RodProblem p = rodProblemFromJson(body.value("problem", Json::object()));
        const std::string kind = body.value("kind", "nodal");
        std::string csv;
        if (kind == "convergence") {
            csv = util::convergenceCsv(thermal::runConvergenceStudy(p, meshListFromJson(body)));
        } else {
            const auto sol = thermal::solveRod(p, countOr(body, "elements", 10));
            if (kind == "nodal") {
                csv = util::feaNodalCsv(sol);
            } else if (kind == "midpoint") {
                csv = util::feaMidpointCsv(thermal::evaluateMidpointError(sol));
            } else {
                throw ValidationError("export kind must be nodal, midpoint or convergence");
            }
        }
        res.set_content(csv, "text/csv");
    }));

    // ---------------------------------------------------------- performance
    s.Post("/api/performance/benchmark", guarded([this](const httplib::Request& req, httplib::Response& res) {
        const perf::BenchmarkConfig cfg = benchmarkConfigFromJson(parseBody(req));
        const std::string id = jobs_.start("benchmark", "benchmark", [cfg](JobContext& ctx) {
            const perf::BenchmarkResult r = perf::runBenchmark(
                cfg, &ctx.cancelRequested, [&ctx](double f, const std::string& m) { ctx.setProgress(f, m); });
            const perf::SystemInfo info = perf::querySystemInfo();
            Json j = toJson(r);
            j["system"] = toJson(info);
            j["summaryCsv"] = util::benchmarkSummaryCsv(r, info);
            j["runsCsv"] = util::benchmarkRunsCsv(r);
            return j;
        });
        sendJson(res, {{"jobId", id}}, 202);
    }));
    s.Get(R"(/api/jobs/([A-Za-z0-9\-]+))", guarded([this](const httplib::Request& req, httplib::Response& res) {
        sendJson(res, jobs_.status(req.matches[1]));
    }));
    s.Post(R"(/api/jobs/([A-Za-z0-9\-]+)/cancel)", guarded([this](const httplib::Request& req, httplib::Response& res) {
        jobs_.cancel(req.matches[1]);
        sendJson(res, {{"ok", true}});
    }));

    // ------------------------------------------------------------- hardware
    s.Get("/api/hardware/devices", guarded([this](const httplib::Request&, httplib::Response& res) {
        Json list = Json::array();
        for (const auto& d : devices_.devices()) list.push_back(toJson(*d));
        sendJson(res, {{"devices", list}, {"simulated", true}});
    }));
    s.Post("/api/hardware/discover", guarded([this](const httplib::Request&, httplib::Response& res) {
        const auto t0 = Clock::now();
        devices_.discover();
        Json list = Json::array();
        for (const auto& d : devices_.devices()) list.push_back(toJson(*d));
        sendJson(res, {{"devices", list},
                       {"simulated", true},
                       {"scanMs", std::chrono::duration<double, std::milli>(Clock::now() - t0).count()}});
    }));
    s.Post(R"(/api/hardware/([a-z0-9]+)/(connect|disconnect|telemetry|fault|recover))",
           guarded([this](const httplib::Request& req, httplib::Response& res) {
               const std::string id = req.matches[1];
               const std::string action = req.matches[2];
               const auto device = devices_.find(id);
               const Json body = parseBody(req);
               const auto timeout = std::chrono::milliseconds(
                   static_cast<long>(numberOr(body, "timeoutMs", static_cast<double>(hw::DeviceManager::kDefaultTimeout.count()))));
               if (timeout.count() < 10 || timeout.count() > 5000) {
                   throw ValidationError("timeoutMs must be between 10 and 5000");
               }
               Json out;
               if (action == "connect") {
                   device->connect(timeout);
               } else if (action == "disconnect") {
                   device->disconnect();
               } else if (action == "telemetry") {
                   out["telemetry"] = toJson(device->requestTelemetry(timeout));
               } else if (action == "fault") {
                   device->injectFault(hw::parseFaultMode(body.value("mode", "telemetry_timeout")));
                   out["armed"] = body.value("mode", "telemetry_timeout");
               } else {  // recover
                   const auto report = devices_.recover(id);
                   out["recovery"] = {{"recovered", report.recovered}, {"attempts", report.attempts}, {"log", report.log}};
               }
               out["device"] = toJson(*device);
               sendJson(res, out);
           }));

    // ------------------------------------------------------------------- ML
    s.Post("/api/ml/backprop", guarded([](const httplib::Request& req, httplib::Response& res) {
        const Json body = parseBody(req);
        const ml::Parameters p0 = parametersFromJson(body.value("params", Json::object()));
        const ml::Sample sample{numberOr(body, "x", 1.0), numberOr(body, "y", 2.0)};
        const double lr = numberOr(body, "learningRate", 0.1);
        const double eps = numberOr(body, "epsilon", 1e-6);
        const std::size_t steps = countOr(body, "steps", 100);
        if (!(eps > 0.0 && eps < 1.0)) throw ValidationError("epsilon must be between 0 and 1");
        const ml::ForwardPass f = ml::forward(p0, sample);
        const ml::BackwardTrace t = ml::backward(p0, sample);
        const ml::Gradients n = ml::numericalGradients(p0, sample, eps);
        const ml::Parameters p1 = ml::gradientStep(p0, t.grads, lr);
        const auto history = ml::train(p0, sample, lr, steps);
        Json loss = Json::array();
        for (const auto& h : history) loss.push_back(h.loss);
        const double errs[4] = {std::fabs(t.grads.dw - n.dw), std::fabs(t.grads.db - n.db),
                                std::fabs(t.grads.dv - n.dv), std::fabs(t.grads.dc - n.dc)};
        bool pass = true;
        for (double e : errs) pass = pass && e <= 1e-5;
        sendJson(res, {{"forward", {{"z", f.z}, {"h", f.h}, {"yHat", f.yHat}, {"loss", f.loss}}},
                       {"chain", {{"dJ_dyHat", t.dJ_dyHat}, {"dJ_dh", t.dJ_dh}, {"dh_dz", t.dh_dz}, {"dJ_dz", t.dJ_dz}}},
                       {"analytic", toJson(t.grads)},
                       {"numerical", toJson(n)},
                       {"absError", {{"w", errs[0]}, {"b", errs[1]}, {"v", errs[2]}, {"c", errs[3]}}},
                       {"tolerance", 1e-5},
                       {"gradientCheckPassed", pass},
                       {"initial", toJson(p0)},
                       {"updated", toJson(p1)},
                       {"lossAfterUpdate", ml::forward(p1, sample).loss},
                       {"lossHistory", loss},
                       {"finalParams", toJson(history.back().params)},
                       {"lossCsv", util::backpropLossCsv(history)}});
    }));

    // ------------------------------------------------------------ static UI
    // Registered last: httplib matches routes in order and this pattern is a catch-all.
    s.Get(R"(/(.*))", [](const httplib::Request& req, httplib::Response& res) {
        std::string path = req.matches[1];
        if (path.empty()) path = "index.html";
        if (path.rfind("api/", 0) == 0) {
            sendError(res, 404, "not_found", "unknown API endpoint /" + path);
            return;
        }
        const EmbeddedAsset* asset = findEmbeddedAsset(path);
        if (asset == nullptr) {
            res.status = 404;
            res.set_content("not found", "text/plain");
            return;
        }
        res.set_header("Cache-Control", "no-cache");
        res.set_content(reinterpret_cast<const char*>(asset->data), asset->size, mimeType(path));
    });
}

}  // namespace wb::api
