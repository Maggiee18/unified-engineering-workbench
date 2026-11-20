#include "server/job_manager.hpp"

#include "workbench/core/errors.hpp"

#include <chrono>

namespace wb::api {

const char* toString(JobStatus s) noexcept {
    switch (s) {
        case JobStatus::Running: return "running";
        case JobStatus::Completed: return "completed";
        case JobStatus::Cancelled: return "cancelled";
        case JobStatus::Failed: return "failed";
    }
    return "unknown";
}

void JobContext::setProgress(double fraction, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_ = fraction;
    message_ = message;
}

JobManager::~JobManager() {
    std::map<std::string, std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs = jobs_;
    }
    for (auto& [id, job] : jobs) job->context->cancelRequested = true;
    for (auto& [id, job] : jobs) {
        if (job->thread.joinable()) job->thread.join();
    }
}

std::string JobManager::start(const std::string& kind, const std::string& exclusiveGroup, Work work) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!exclusiveGroup.empty()) {
        for (const auto& [id, job] : jobs_) {
            if (job->group == exclusiveGroup && job->status == JobStatus::Running) {
                throw ValidationError("a " + exclusiveGroup + " job is already running (" + id +
                                      "); wait for it or cancel it first");
            }
        }
    }
    pruneFinishedLocked();
    auto job = std::make_shared<Job>();
    job->id = "job-" + std::to_string(nextId_++);
    job->kind = kind;
    job->group = exclusiveGroup;
    job->thread = std::thread([job, work = std::move(work)] {
        const auto t0 = std::chrono::steady_clock::now();
        try {
            Json result = work(*job->context);
            std::lock_guard<std::mutex> resultLock(job->resultMutex);
            job->result = std::move(result);
            job->status = JobStatus::Completed;
        } catch (const OperationCancelled&) {
            job->status = JobStatus::Cancelled;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> resultLock(job->resultMutex);
            job->error = e.what();
            job->status = JobStatus::Failed;
        }
        std::lock_guard<std::mutex> resultLock(job->resultMutex);
        job->elapsedSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    });
    jobs_[job->id] = job;
    return job->id;
}

Json JobManager::status(const std::string& id) const {
    std::shared_ptr<Job> job;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = jobs_.find(id);
        if (it == jobs_.end()) throw ValidationError("unknown job id '" + id + "'");
        job = it->second;
    }
    Json out{{"id", job->id}, {"kind", job->kind}, {"status", toString(job->status.load())}};
    {
        std::lock_guard<std::mutex> lock(job->context->mutex_);
        out["progress"] = job->context->progress_;
        out["message"] = job->context->message_;
    }
    out["cancelRequested"] = job->context->cancelRequested.load();
    std::lock_guard<std::mutex> lock(job->resultMutex);
    if (job->status == JobStatus::Completed) out["result"] = job->result;
    if (job->status == JobStatus::Failed) out["error"] = job->error;
    if (job->status != JobStatus::Running) out["elapsedSeconds"] = job->elapsedSeconds;
    return out;
}

void JobManager::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = jobs_.find(id);
    if (it == jobs_.end()) throw ValidationError("unknown job id '" + id + "'");
    it->second->context->cancelRequested = true;
}

void JobManager::pruneFinishedLocked() {
    // Keep memory bounded: drop finished jobs beyond the 16 most recent.
    while (jobs_.size() > 16) {
        auto victim = jobs_.end();
        for (auto it = jobs_.begin(); it != jobs_.end(); ++it) {
            if (it->second->status != JobStatus::Running) {
                victim = it;
                break;
            }
        }
        if (victim == jobs_.end()) return;
        if (victim->second->thread.joinable()) victim->second->thread.join();
        jobs_.erase(victim);
    }
}

}  // namespace wb::api
