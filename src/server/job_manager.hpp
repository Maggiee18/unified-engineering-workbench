// Background jobs for long computations (benchmarks) so HTTP handlers return
// immediately and the UI stays responsive. A job publishes its result only
// when it completes; a cancelled or failed job never exposes partial results.
#pragma once

#include "server/json_mapping.hpp"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace wb::api {

enum class JobStatus { Running, Completed, Cancelled, Failed };
const char* toString(JobStatus s) noexcept;

class JobContext {
public:
    std::atomic<bool> cancelRequested{false};
    void setProgress(double fraction, const std::string& message);

private:
    friend class JobManager;
    std::mutex mutex_;
    double progress_ = 0.0;
    std::string message_;
};

class JobManager {
public:
    using Work = std::function<Json(JobContext&)>;

    ~JobManager();  // requests cancellation and joins all job threads

    /// Starts `work` on its own thread. If `exclusiveGroup` is non-empty and a
    /// job of that group is still running, throws wb::ValidationError (409).
    std::string start(const std::string& kind, const std::string& exclusiveGroup, Work work);
    Json status(const std::string& id) const;  // throws ValidationError if unknown
    void cancel(const std::string& id);

private:
    struct Job {
        std::string id, kind, group;
        std::shared_ptr<JobContext> context = std::make_shared<JobContext>();
        std::atomic<JobStatus> status{JobStatus::Running};
        std::mutex resultMutex;
        Json result;
        std::string error;
        double elapsedSeconds = 0.0;
        std::thread thread;
    };
    void pruneFinishedLocked();

    mutable std::mutex mutex_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    std::uint64_t nextId_ = 1;
};

}  // namespace wb::api
