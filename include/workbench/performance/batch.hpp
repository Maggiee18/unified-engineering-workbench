// Batch of independent thermal solves: reproducible generation, serial and
// parallel execution, and the benchmark protocol (warm-up + 5 repetitions).
#pragma once

#include "workbench/thermal/rod_fea.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wb::perf {

struct BatchConfig {
    std::size_t caseCount = 2000;
    std::size_t elementsPerCase = 2000;
    std::uint64_t seed = 20262027;
    double conductivityMin = 1.0, conductivityMax = 400.0;     // W/(m K)
    double heatGenerationMin = 1e3, heatGenerationMax = 1e5;   // W/m^3
    void validate() const;
};

struct BatchCase {
    std::size_t index = 0;
    thermal::RodProblem problem;
};

struct CaseResult {
    std::size_t index = 0;
    double conductivity = 0.0;
    double heatGeneration = 0.0;
    double maxTemperature = 0.0;     // max nodal T [C]
    double xAtMaxTemperature = 0.0;  // [m]
    double maxMidpointError = 0.0;   // [C]
    std::vector<double> temperature; // nodal field, kept for agreement checks
};

/// Deterministic across compilers/platforms: uses std::mt19937_64 (fully
/// specified by the standard) and its own [0,1) mapping, because
/// std::uniform_real_distribution is implementation-defined.
std::vector<BatchCase> generateBatch(const BatchConfig& config);

/// Cooperative cancellation flag shared between UI/transport and workers.
using CancelToken = std::atomic<bool>;

/// Single solve used identically by both serial and parallel paths.
CaseResult solveCase(const BatchCase& c, std::size_t elements);

std::vector<CaseResult> runSerial(const std::vector<BatchCase>& cases, std::size_t elements,
                                  const CancelToken* cancel = nullptr);

class ThreadPool;
/// Runs every case on the pool; result[i] always corresponds to cases[i].
std::vector<CaseResult> runParallel(const std::vector<BatchCase>& cases, std::size_t elements, ThreadPool& pool,
                                    const CancelToken* cancel = nullptr);

/// Max |T_a - T_b| over all cases and nodes [C]. Throws if shapes differ.
double maxAbsoluteDifference(const std::vector<CaseResult>& a, const std::vector<CaseResult>& b);

struct BenchmarkConfig {
    BatchConfig batch;
    std::vector<std::size_t> workerCounts = {1, 2, 3, 4};
    std::size_t warmupRuns = 1;
    std::size_t measuredRuns = 5;
    double agreementTolerance = 1e-8;  // C
};

struct WorkerTiming {
    std::size_t workers = 0;
    bool skipped = false;
    std::string skipReason;
    std::vector<double> runSeconds;   // the measured repetitions
    double medianSeconds = 0.0;
    double speedup = 0.0;             // T1 / Tp  (T1 = pool with 1 worker)
    double efficiency = 0.0;          // speedup / p
    double speedupVsSerial = 0.0;     // T_serial / Tp
    double maxAbsDiffVsSerial = 0.0;  // C
    bool agreementPassed = false;
};

struct BenchmarkResult {
    BenchmarkConfig config;
    std::vector<double> serialRunSeconds;
    double serialMedianSeconds = 0.0;
    std::vector<WorkerTiming> timings;
    std::size_t logicalCpus = 0;
    bool allAgreementPassed = false;
    std::string note;  // e.g. single-CPU disclosure
};

/// progress(fraction in [0,1], message). Throws wb::OperationCancelled.
using ProgressCallback = std::function<void(double, const std::string&)>;

BenchmarkResult runBenchmark(const BenchmarkConfig& config, const CancelToken* cancel = nullptr,
                             const ProgressCallback& progress = {});

double median(std::vector<double> values);

}  // namespace wb::perf
