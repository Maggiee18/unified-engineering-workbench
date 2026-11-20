#include "utilities/csv_export.hpp"

#include "workbench/core/errors.hpp"

#include <charconv>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace wb::util {

std::string formatNumber(double value) {
    char buffer[64];
    const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::general, 17);
    return ec == std::errc() ? std::string(buffer, ptr) : std::string("nan");
}

std::string feaNodalCsv(const thermal::RodSolution& s) {
    std::ostringstream out;
    out << "# 1D steady thermal FEA, " << s.elements << " linear elements; units: x [m], T [C]\n";
    out << "node,x_m,T_fe_C,T_exact_C,abs_error_C\n";
    for (std::size_t i = 0; i < s.x.size(); ++i) {
        const double exact = thermal::exactTemperature(s.problem, s.x[i]);
        out << i << ',' << formatNumber(s.x[i]) << ',' << formatNumber(s.temperature[i]) << ','
            << formatNumber(exact) << ',' << formatNumber(std::abs(s.temperature[i] - exact)) << '\n';
    }
    return out.str();
}

std::string feaMidpointCsv(const thermal::MidpointErrorReport& r) {
    std::ostringstream out;
    out << "# Element-midpoint comparison using FE interpolation, N=" << r.elements << "; units: x [m], T [C]\n";
    out << "element,x_mid_m,T_fe_interp_C,T_exact_C,abs_error_C\n";
    for (std::size_t e = 0; e < r.midpointX.size(); ++e) {
        out << e << ',' << formatNumber(r.midpointX[e]) << ',' << formatNumber(r.midpointFe[e]) << ','
            << formatNumber(r.midpointExact[e]) << ',' << formatNumber(std::abs(r.midpointFe[e] - r.midpointExact[e]))
            << '\n';
    }
    return out.str();
}

std::string convergenceCsv(const std::vector<thermal::ConvergenceRow>& rows) {
    std::ostringstream out;
    out << "# Mesh convergence; errors in C, h in m; predicted = q h^2 / (8 k)\n";
    out << "elements,h_m,max_midpoint_error_C,predicted_midpoint_error_C,max_nodal_error_C,observed_order\n";
    for (const auto& row : rows) {
        out << row.report.elements << ',' << formatNumber(row.report.h) << ','
            << formatNumber(row.report.maxMidpointError) << ',' << formatNumber(row.report.predictedMidpointError)
            << ',' << formatNumber(row.report.maxNodalError) << ','
            << (row.observedOrder == 0.0 ? std::string("") : formatNumber(row.observedOrder)) << '\n';
    }
    return out.str();
}

std::string benchmarkSummaryCsv(const perf::BenchmarkResult& r, const perf::SystemInfo& info) {
    std::ostringstream out;
    out << "# cpu=" << info.cpuModel << "; physical_cores=" << info.physicalCores
        << "; logical_cpus=" << info.logicalCpus << "; ram_bytes=" << info.totalRamBytes << '\n';
    out << "# os=" << info.operatingSystem << "; compiler=" << info.compiler << "; build=" << info.buildType
        << "; flags=" << info.optimisationFlags << '\n';
    out << "# cases=" << r.config.batch.caseCount << "; elements_per_case=" << r.config.batch.elementsPerCase
        << "; seed=" << r.config.batch.seed << "; warmups=" << r.config.warmupRuns
        << "; repetitions=" << r.config.measuredRuns << '\n';
    if (!r.note.empty()) out << "# note=" << r.note << '\n';
    out << "mode,workers,median_s,speedup_T1_over_Tp,efficiency,speedup_vs_serial,max_abs_diff_vs_serial_C,"
           "agreement_pass,skipped\n";
    out << "serial,1," << formatNumber(r.serialMedianSeconds) << ",,,1,0,true,false\n";
    for (const auto& t : r.timings) {
        if (t.skipped) {
            out << "parallel," << t.workers << ",,,,,,,true\n";
            continue;
        }
        out << "parallel," << t.workers << ',' << formatNumber(t.medianSeconds) << ',' << formatNumber(t.speedup)
            << ',' << formatNumber(t.efficiency) << ',' << formatNumber(t.speedupVsSerial) << ','
            << formatNumber(t.maxAbsDiffVsSerial) << ',' << (t.agreementPassed ? "true" : "false") << ",false\n";
    }
    return out.str();
}

std::string benchmarkRunsCsv(const perf::BenchmarkResult& r) {
    std::ostringstream out;
    out << "mode,workers,repetition,seconds\n";
    for (std::size_t i = 0; i < r.serialRunSeconds.size(); ++i) {
        out << "serial,1," << i + 1 << ',' << formatNumber(r.serialRunSeconds[i]) << '\n';
    }
    for (const auto& t : r.timings) {
        for (std::size_t i = 0; i < t.runSeconds.size(); ++i) {
            out << "parallel," << t.workers << ',' << i + 1 << ',' << formatNumber(t.runSeconds[i]) << '\n';
        }
    }
    return out.str();
}

std::string backpropLossCsv(const std::vector<ml::TrainingRecord>& history) {
    std::ostringstream out;
    out << "step,w,b,v,c,loss\n";
    for (const auto& rec : history) {
        out << rec.step << ',' << formatNumber(rec.params.w) << ',' << formatNumber(rec.params.b) << ','
            << formatNumber(rec.params.v) << ',' << formatNumber(rec.params.c) << ',' << formatNumber(rec.loss) << '\n';
    }
    return out.str();
}

void writeTextFile(const std::string& path, const std::string& content) {
    namespace fs = std::filesystem;
    const fs::path p = fs::u8path(path);
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) throw ValidationError("cannot write file: " + path);
    out << content;
    if (!out) throw ValidationError("failed while writing file: " + path);
}

}  // namespace wb::util
