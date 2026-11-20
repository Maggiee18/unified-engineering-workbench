// Fixed-size (bounded) worker pool built on std::thread.
//
// The number of OS threads is fixed at construction and never grows, so a
// batch of 10 000 cases runs on exactly `workers` threads. Tasks are queued
// FIFO; submit() returns a std::future that also transports exceptions.
#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <type_traits>
#include <vector>

namespace wb::perf {

class ThreadPool {
public:
    /// Throws wb::ValidationError if workers == 0.
    explicit ThreadPool(std::size_t workers);
    ~ThreadPool();  // drains the queue, then joins every worker (RAII)

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t size() const noexcept { return threads_.size(); }

    template <typename F>
    std::future<std::invoke_result_t<F>> submit(F&& task) {
        using R = std::invoke_result_t<F>;
        auto packaged = std::make_shared<std::packaged_task<R()>>(std::forward<F>(task));
        std::future<R> result = packaged->get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.emplace([packaged] { (*packaged)(); });
        }
        wake_.notify_one();
        return result;
    }

private:
    void workerLoop();

    std::vector<std::thread> threads_;
    std::queue<std::function<void()>> queue_;
    std::mutex mutex_;
    std::condition_variable wake_;
    bool stopping_ = false;
};

/// Logical CPUs reported by the OS (never 0).
std::size_t logicalCpuCount() noexcept;

}  // namespace wb::perf
