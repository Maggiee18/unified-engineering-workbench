#include "workbench/performance/thread_pool.hpp"

#include "workbench/core/errors.hpp"

namespace wb::perf {

ThreadPool::ThreadPool(std::size_t workers) {
    if (workers == 0) throw ValidationError("worker count must be at least 1");
    threads_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i) threads_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    wake_.notify_all();
    for (auto& t : threads_) t.join();
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_ && queue_.empty()) return;
            task = std::move(queue_.front());
            queue_.pop();
        }
        task();  // packaged_task captures any exception into its future
    }
}

std::size_t logicalCpuCount() noexcept {
    const unsigned n = std::thread::hardware_concurrency();
    return n == 0 ? 1 : n;
}

}  // namespace wb::perf
