#include "workbench/core/errors.hpp"
#include "workbench/performance/batch.hpp"
#include "workbench/performance/system_info.hpp"
#include "workbench/performance/thread_pool.hpp"

#include <doctest.h>

#include <atomic>
#include <random>
#include <set>
#include <thread>

using namespace wb;
using namespace wb::perf;

TEST_CASE("ThreadPool: runs tasks, returns values and propagates exceptions") {
    ThreadPool pool(3);
    CHECK(pool.size() == 3);
    auto a = pool.submit([] { return 6 * 7; });
    auto b = pool.submit([]() -> int { throw std::runtime_error("boom"); });
    CHECK(a.get() == 42);
    CHECK_THROWS_WITH(b.get(), "boom");
}

TEST_CASE("ThreadPool: bounded - never uses more than `workers` threads") {
    ThreadPool pool(2);
    std::mutex m;
    std::set<std::thread::id> ids;
    std::vector<std::future<void>> fs;
    for (int i = 0; i < 64; ++i) {
        fs.push_back(pool.submit([&] {
            std::lock_guard<std::mutex> lock(m);
            ids.insert(std::this_thread::get_id());
        }));
    }
    for (auto& f : fs) f.get();
    CHECK(ids.size() <= 2);
}

TEST_CASE("ThreadPool negative: zero workers rejected") { CHECK_THROWS_AS(ThreadPool(0), ValidationError); }

TEST_CASE("Batch generator: reproducible with fixed seed, varies with seed, positive k and q") {
    BatchConfig c;
    c.caseCount = 50;
    c.elementsPerCase = 10;
    const auto a = generateBatch(c);
    const auto b = generateBatch(c);
    REQUIRE(a.size() == 50);
    bool allSame = true;
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].problem.conductivity == b[i].problem.conductivity);  // bitwise identical
        CHECK(a[i].problem.conductivity >= c.conductivityMin);
        CHECK(a[i].problem.conductivity <= c.conductivityMax);
        CHECK(a[i].problem.heatGeneration > 0.0);
        if (i > 0 && a[i].problem.conductivity != a[0].problem.conductivity) allSame = false;
    }
    CHECK_FALSE(allSame);
    c.seed += 1;
    CHECK(generateBatch(c)[0].problem.conductivity != a[0].problem.conductivity);
}

TEST_CASE("Batch generator: first case is pinned (cross-platform reproducibility guard)") {
    // mt19937_64 is fully specified by the standard and the [0,1) mapping is
    // our own, so this value must be identical on Linux GCC and Windows MinGW.
    BatchConfig c;
    c.caseCount = 1;
    c.elementsPerCase = 1;
    std::mt19937_64 reference(c.seed);
    const double u = static_cast<double>(reference() >> 11) * 0x1.0p-53;
    CHECK(generateBatch(c)[0].problem.conductivity == c.conductivityMin + (c.conductivityMax - c.conductivityMin) * u);
}

TEST_CASE("Serial and parallel results agree and keep deterministic input order") {
    BatchConfig c;
    c.caseCount = 97;  // deliberately not a multiple of the chunk count
    c.elementsPerCase = 64;
    const auto cases = generateBatch(c);
    const auto serial = runSerial(cases, c.elementsPerCase);
    for (std::size_t workers : {1u, 2u, 3u, 4u}) {
        ThreadPool pool(workers);
        const auto parallel = runParallel(cases, c.elementsPerCase, pool);
        REQUIRE(parallel.size() == serial.size());
        for (std::size_t i = 0; i < parallel.size(); ++i) CHECK(parallel[i].index == i);
        CHECK(maxAbsoluteDifference(serial, parallel) <= 1e-8);
    }
}

TEST_CASE("Each batch case is a correct thermal solve") {
    BatchConfig c;
    c.caseCount = 5;
    c.elementsPerCase = 20;
    for (const auto& bc : generateBatch(c)) {
        const CaseResult r = solveCase(bc, 20);
        const double h = 1.0 / 20.0;
        CHECK(r.maxMidpointError ==
              doctest::Approx(bc.problem.heatGeneration * h * h / (8.0 * bc.problem.conductivity)).epsilon(1e-6));
    }
}

TEST_CASE("Cancellation stops serial and parallel runs without partial results") {
    BatchConfig c;
    c.caseCount = 200;
    c.elementsPerCase = 50;
    const auto cases = generateBatch(c);
    CancelToken cancel{true};
    CHECK_THROWS_AS(runSerial(cases, 50, &cancel), OperationCancelled);
    ThreadPool pool(2);
    CHECK_THROWS_AS(runParallel(cases, 50, pool, &cancel), OperationCancelled);
}

TEST_CASE("Benchmark protocol: warm-up + repetitions, median, speedup and agreement") {
    BenchmarkConfig cfg;
    cfg.batch.caseCount = 40;
    cfg.batch.elementsPerCase = 50;
    cfg.workerCounts = {1, 2, 1000};  // 1000 must be skipped (exceeds logical CPUs)
    cfg.measuredRuns = 5;
    std::vector<double> progress;
    const BenchmarkResult r = runBenchmark(cfg, nullptr, [&](double f, const std::string&) { progress.push_back(f); });
    CHECK(r.serialRunSeconds.size() == 5);
    REQUIRE(r.timings.size() == 3);
    CHECK(r.timings[0].runSeconds.size() == 5);
    CHECK(r.timings[0].speedup == doctest::Approx(1.0));
    CHECK(r.timings[2].skipped);
    CHECK(r.allAgreementPassed);
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.back() == doctest::Approx(1.0));
}

TEST_CASE("Benchmark negative: invalid configuration") {
    BenchmarkConfig cfg;
    cfg.workerCounts = {0};
    CHECK_THROWS_AS(runBenchmark(cfg), ValidationError);
    cfg = BenchmarkConfig{};
    cfg.batch.caseCount = 0;
    CHECK_THROWS_AS(runBenchmark(cfg), ValidationError);
    cfg = BenchmarkConfig{};
    cfg.batch.caseCount = 1'000'000;
    cfg.batch.elementsPerCase = 100'000;  // memory guard
    CHECK_THROWS_AS(runBenchmark(cfg), ValidationError);
}

TEST_CASE("median helper") {
    CHECK(median({3, 1, 2}) == 2.0);
    CHECK(median({4, 1, 2, 3}) == 2.5);
    CHECK(median({}) == 0.0);
}

TEST_CASE("System information is populated") {
    const SystemInfo s = querySystemInfo();
    CHECK(s.logicalCpus >= 1);
    CHECK_FALSE(s.cpuModel.empty());
    CHECK_FALSE(s.compiler.empty());
}
