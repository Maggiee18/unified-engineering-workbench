// CSV writers for every exportable result (UI downloads and results/ folder).
// All numbers use '.' as decimal separator and 17 significant digits so the
// files round-trip exactly.
#pragma once

#include "workbench/ml/backprop.hpp"
#include "workbench/performance/batch.hpp"
#include "workbench/performance/system_info.hpp"
#include "workbench/thermal/rod_fea.hpp"

#include <string>
#include <vector>

namespace wb::util {

std::string feaNodalCsv(const thermal::RodSolution& s);
std::string feaMidpointCsv(const thermal::MidpointErrorReport& r);
std::string convergenceCsv(const std::vector<thermal::ConvergenceRow>& rows);
std::string benchmarkSummaryCsv(const perf::BenchmarkResult& r, const perf::SystemInfo& info);
std::string benchmarkRunsCsv(const perf::BenchmarkResult& r);
std::string backpropLossCsv(const std::vector<ml::TrainingRecord>& history);

/// Writes text to a file, creating parent directories. Throws ValidationError.
void writeTextFile(const std::string& path, const std::string& content);

/// Formats a double with 17 significant digits, locale-independent.
std::string formatNumber(double value);

}  // namespace wb::util
