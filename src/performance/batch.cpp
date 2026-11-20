#include "workbench/performance/batch.hpp"

#include "workbench/performance/thread_pool.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <future>
#include <random>
#include <stdexcept>

namespace wb::perf {
namespace {

/// Uniform double in [0, 1) from the top 53 bits of a 64-bit draw.
double unitInterval(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * 0x1.0p-53;
}

void checkCancelled(const CancelToken* cancel) {
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) throw OperationCancelled();
}

using Clock = std::chrono::steady_clock;
double secondsSince(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

}  // namespace

void BatchConfig::validate() const {
    if (caseCount < 1 || caseCount > 1'000'000) throw ValidationError("Number of cases must be between 1 and 1,000,000");
    if (elementsPerCase < 1 || elementsPerCase > 200'000) {
        throw ValidationError("Mesh size must be between 1 and 200,000 elements per case");
    }
    // Memory guard: every case keeps its nodal field for the agreement check.
    const double megabytes = static_cast<double>(caseCount) * static_cast<double>(elementsPerCase + 1) * 8.0 / 1e6;
    if (megabytes > 512.0) {  // two result sets are held during a benchmark
        throw ValidationError("cases x mesh size needs about " + std::to_string(static_cast<long>(megabytes)) +
                              " MB per result set; reduce it below 512 MB");
    }
    if (!(conductivityMin > 0.0 && conductivityMax >= conductivityMin)) {
        throw ValidationError("Conductivity range must be positive with min <= max");
    }
    if (!(heatGenerationMin > 0.0 && heatGenerationMax >= heatGenerationMin)) {
        throw ValidationError("Heat generation range must be positive with min <= max");
    }
}

std::vector<BatchCase> generateBatch(const BatchConfig& config) {
    config.validate();
    std::mt19937_64 rng(config.seed);
    std::vector<BatchCase> cases(config.caseCount);
    for (std::size_t i = 0; i < config.caseCount; ++i) {
        cases[i].index = i;
        // Base geometry/BCs from the assignment; only k and q vary.
        cases[i].problem.conductivity =
            config.conductivityMin + (config.conductivityMax - config.conductivityMin) * unitInterval(rng);
        cases[i].problem.heatGeneration =
            config.heatGenerationMin + (config.heatGenerationMax - config.heatGenerationMin) * unitInterval(rng);
    }
    return cases;
}

CaseResult solveCase(const BatchCase& c, std::size_t elements) {
    const thermal::RodSolution s = thermal::solveRod(c.problem, elements);
    const thermal::MidpointErrorReport err = thermal::evaluateMidpointError(s);
    CaseResult r;
    r.index = c.index;
    r.conductivity = c.problem.conductivity;
    r.heatGeneration = c.problem.heatGeneration;
    const auto it = std::max_element(s.temperature.begin(), s.temperature.end());
    r.maxTemperature = *it;
    r.xAtMaxTemperature = s.x[static_cast<std::size_t>(it - s.temperature.begin())];
    r.maxMidpointError = err.maxMidpointError;
    r.temperature = s.temperature;
    return r;
}

std::vector<CaseResult> runSerial(const std::vector<BatchCase>& cases, std::size_t elements,
                                  const CancelToken* cancel) {
    std::vector<CaseResult> results(cases.size());
    for (std::size_t i = 0; i < cases.size(); ++i) {
        checkCancelled(cancel);
        results[i] = solveCase(cases[i], elements);
    }
    return results;
}

std::vector<CaseResult> runParallel(const std::vector<BatchCase>& cases, std::size_t elements, ThreadPool& pool,
                                    const CancelToken* cancel) {
    std::vector<CaseResult> results(cases.size());
    // ~8 chunks per worker balances load without flooding the queue.
    const std::size_t chunks = std::min(cases.size(), pool.size() * 8);
    const std::size_t chunkSize = (cases.size() + chunks - 1) / chunks;
    std::vector<std::future<void>> pending;
    pending.reserve(chunks);
    for (std::size_t begin = 0; begin < cases.size(); begin += chunkSize) {
        const std::size_t end = std::min(cases.size(), begin + chunkSize);
        // Each task writes a disjoint slice of `results`: no locking needed and
        // output order equals input order regardless of completion order.
        pending.push_back(pool.submit([&, begin, end] {
            for (std::size_t i = begin; i < end; ++i) {
                checkCancelled(cancel);
                results[i] = solveCase(cases[i], elements);
            }
        }));
    }
    // Wait for *all* tasks before rethrowing so no task outlives `results`.
    std::exception_ptr firstError;
    for (auto& f : pending) {
        try {
            f.get();
        } catch (...) {
            if (!firstError) firstError = std::current_exception();
        }
    }
    if (firstError) std::rethrow_exception(firstError);
    return results;
}

double maxAbsoluteDifference(const std::vector<CaseResult>& a, const std::vector<CaseResult>& b) {
    if (a.size() != b.size()) throw std::invalid_argument("result sets have different case counts");
    double worst = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].index != b[i].index || a[i].temperature.size() != b[i].temperature.size()) {
            throw std::invalid_argument("result sets are not aligned at case " + std::to_string(i));
        }
        for (std::size_t j = 0; j < a[i].temperature.size(); ++j) {
            worst = std::max(worst, std::fabs(a[i].temperature[j] - b[i].temperature[j]));
        }
    }
    return worst;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return n % 2 == 1 ? values[n / 2] : 0.5 * (values[n / 2 - 1] + values[n / 2]);
}

BenchmarkResult runBenchmark(const BenchmarkConfig& config, const CancelToken* cancel,
                             const ProgressCallback& progress) {
    if (config.measuredRuns < 1) throw ValidationError("measured repetitions must be at least 1");
    if (config.workerCounts.empty()) throw ValidationError("select at least one worker count");
    for (std::size_t w : config.workerCounts) {
        if (w == 0) throw ValidationError("worker count must be at least 1");
    }

    BenchmarkResult out;
    out.config = config;
    out.logicalCpus = logicalCpuCount();

    // Workload generation is outside every timed region.
    const std::vector<BatchCase> cases = generateBatch(config.batch);
    const std::size_t elements = config.batch.elementsPerCase;

    const std::size_t runsPerConfig = config.warmupRuns + config.measuredRuns;
    const std::size_t totalRuns = runsPerConfig * (1 + config.workerCounts.size());
    std::size_t completed = 0;
    auto report = [&](const std::string& message) {
        ++completed;
        if (progress) progress(static_cast<double>(completed) / static_cast<double>(totalRuns), message);
    };

    // Serial reference (plain loop, no pool).
    std::vector<CaseResult> serialResults;
    for (std::size_t run = 0; run < runsPerConfig; ++run) {
        checkCancelled(cancel);
        const auto start = Clock::now();
        auto results = runSerial(cases, elements, cancel);
        const double seconds = secondsSince(start);
        if (run >= config.warmupRuns) out.serialRunSeconds.push_back(seconds);
        serialResults = std::move(results);
        report(run < config.warmupRuns ? "serial warm-up" : "serial run " + std::to_string(run - config.warmupRuns + 1));
    }
    out.serialMedianSeconds = median(out.serialRunSeconds);

    for (std::size_t workers : config.workerCounts) {
        WorkerTiming t;
        t.workers = workers;
        if (workers > out.logicalCpus) {
            t.skipped = true;
            t.skipReason = "exceeds the " + std::to_string(out.logicalCpus) + " logical CPU(s) available";
            out.timings.push_back(t);
            completed += runsPerConfig;
            if (progress) progress(static_cast<double>(completed) / static_cast<double>(totalRuns), "skipped p=" + std::to_string(workers));
            continue;
        }
        ThreadPool pool(workers);  // thread creation is excluded from timings
        std::vector<CaseResult> parallelResults;
        for (std::size_t run = 0; run < runsPerConfig; ++run) {
            checkCancelled(cancel);
            const auto start = Clock::now();
            auto results = runParallel(cases, elements, pool, cancel);
            const double seconds = secondsSince(start);
            if (run >= config.warmupRuns) t.runSeconds.push_back(seconds);
            parallelResults = std::move(results);
            report("p=" + std::to_string(workers) +
                   (run < config.warmupRuns ? " warm-up" : " run " + std::to_string(run - config.warmupRuns + 1)));
        }
        t.medianSeconds = median(t.runSeconds);
        t.maxAbsDiffVsSerial = maxAbsoluteDifference(serialResults, parallelResults);
        t.agreementPassed = t.maxAbsDiffVsSerial <= config.agreementTolerance;
        out.timings.push_back(std::move(t));
    }

    // Speedup relative to the 1-worker pool run (T1/Tp), plus vs. plain serial.
    double t1 = 0.0;
    for (const auto& t : out.timings) {
        if (!t.skipped && t.workers == 1) t1 = t.medianSeconds;
    }
    out.allAgreementPassed = true;
    for (auto& t : out.timings) {
        if (t.skipped) continue;
        const double base = t1 > 0.0 ? t1 : out.serialMedianSeconds;
        t.speedup = base / t.medianSeconds;
        t.efficiency = t.speedup / static_cast<double>(t.workers);
        t.speedupVsSerial = out.serialMedianSeconds / t.medianSeconds;
        out.allAgreementPassed = out.allAgreementPassed && t.agreementPassed;
    }
    if (out.logicalCpus == 1) {
        out.note = "Only one logical CPU is available: the parallel path was exercised with 1 worker, "
                   "but speedup cannot be assessed on this machine.";
    }
    return out;
}

}  // namespace wb::perf
