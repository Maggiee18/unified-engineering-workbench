#include "server/json_mapping.hpp"

#include "workbench/core/errors.hpp"

#include <cmath>

namespace wb::api {

double numberOr(const Json& j, const char* key, double fallback) {
    if (!j.is_object() || !j.contains(key) || j.at(key).is_null()) return fallback;
    const Json& v = j.at(key);
    if (!v.is_number()) throw ValidationError(std::string("'") + key + "' must be a number");
    const double d = v.get<double>();
    if (!std::isfinite(d)) throw ValidationError(std::string("'") + key + "' must be finite");
    return d;
}

std::size_t countOr(const Json& j, const char* key, std::size_t fallback) {
    if (!j.is_object() || !j.contains(key) || j.at(key).is_null()) return fallback;
    const Json& v = j.at(key);
    if (!v.is_number_integer() && !(v.is_number() && std::floor(v.get<double>()) == v.get<double>())) {
        throw ValidationError(std::string("'") + key + "' must be a whole number");
    }
    const double d = v.get<double>();
    if (d < 0) throw ValidationError(std::string("'") + key + "' must not be negative");
    if (d > 1e12) throw ValidationError(std::string("'") + key + "' is too large");
    return static_cast<std::size_t>(d);
}

// ------------------------------------------------------------------ geometry

Json toJson(const geometry::StlMesh& mesh) {
    std::vector<float> positions, normals;
    positions.reserve(mesh.triangles.size() * 9);
    normals.reserve(mesh.triangles.size() * 9);
    for (const auto& t : mesh.triangles) {
        for (const auto& v : t.vertex) {
            positions.insert(positions.end(), {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)});
            normals.insert(normals.end(), {static_cast<float>(t.normal.x), static_cast<float>(t.normal.y),
                                           static_cast<float>(t.normal.z)});
        }
    }
    const auto& b = mesh.bounds;
    return {{"solidName", mesh.solidName},
            {"triangleCount", mesh.triangles.size()},
            {"vertexCount", mesh.triangles.size() * 3},
            {"surfaceArea", mesh.surfaceArea},
            {"degenerateTriangles", mesh.degenerateTriangles},
            {"bounds", {{"min", {b.min.x, b.min.y, b.min.z}}, {"max", {b.max.x, b.max.y, b.max.z}}}},
            {"diagonal", b.diagonal()},
            {"warnings", mesh.warnings},
            {"positions", positions},
            {"normals", normals}};
}

Json toJson(const thermal::NodalDataset& data) {
    Json ids = Json::array(), xyz = Json::array(), temps = Json::array();
    for (const auto& n : data.nodes) {
        ids.push_back(n.nodeId);
        xyz.push_back(n.x);
        xyz.push_back(n.y);
        xyz.push_back(n.z);
        temps.push_back(n.temperatureC);
    }
    return {{"nodeCount", data.nodes.size()},
            {"minTemperatureC", data.minTemperatureC},
            {"maxTemperatureC", data.maxTemperatureC},
            {"bounds", {{"min", {data.minCoord[0], data.minCoord[1], data.minCoord[2]}},
                        {"max", {data.maxCoord[0], data.maxCoord[1], data.maxCoord[2]}}}},
            {"comments", data.comments},
            {"warnings", data.warnings},
            {"nodeIds", ids},
            {"positions", xyz},
            {"temperatures", temps}};
}

// ------------------------------------------------------------------- thermal

thermal::RodProblem rodProblemFromJson(const Json& j) {
    thermal::RodProblem p;
    p.length = numberOr(j, "L", p.length);
    p.area = numberOr(j, "A", p.area);
    p.conductivity = numberOr(j, "k", p.conductivity);
    p.heatGeneration = numberOr(j, "q", p.heatGeneration);
    p.temperatureLeft = numberOr(j, "T0", p.temperatureLeft);
    p.temperatureRight = numberOr(j, "TL", p.temperatureRight);
    p.validate();
    return p;
}

Json toJson(const thermal::RodProblem& p) {
    return {{"L", p.length}, {"A", p.area}, {"k", p.conductivity}, {"q", p.heatGeneration},
            {"T0", p.temperatureLeft}, {"TL", p.temperatureRight}};
}

Json toJson(const thermal::RodSolution& s, const thermal::MidpointErrorReport& r, std::size_t exactSamples) {
    Json exactX = Json::array(), exactT = Json::array();
    for (std::size_t i = 0; i <= exactSamples; ++i) {
        const double x = s.problem.length * static_cast<double>(i) / static_cast<double>(exactSamples);
        exactX.push_back(x);
        exactT.push_back(thermal::exactTemperature(s.problem, x));
    }
    Json midErr = Json::array();
    for (std::size_t e = 0; e < r.midpointX.size(); ++e) midErr.push_back(r.midpointFe[e] - r.midpointExact[e]);
    // Cap transferred nodal data for very fine meshes; summary stays exact.
    const bool includeNodes = s.x.size() <= 20001;
    return {{"problem", toJson(s.problem)},
            {"elements", s.elements},
            {"h", s.elementSize()},
            {"nodesIncluded", includeNodes},
            {"x", includeNodes ? Json(s.x) : Json::array()},
            {"T", includeNodes ? Json(s.temperature) : Json::array()},
            {"exactX", exactX},
            {"exactT", exactT},
            {"midpointX", includeNodes ? Json(r.midpointX) : Json::array()},
            {"midpointFe", includeNodes ? Json(r.midpointFe) : Json::array()},
            {"midpointExact", includeNodes ? Json(r.midpointExact) : Json::array()},
            {"midpointSignedError", includeNodes ? midErr : Json::array()},
            {"maxMidpointError", r.maxMidpointError},
            {"maxNodalError", r.maxNodalError},
            {"predictedMidpointError", r.predictedMidpointError},
            {"reactionLeftW", s.reactionLeft},
            {"reactionRightW", s.reactionRight},
            {"heatGeneratedW", s.problem.heatGeneration * s.problem.area * s.problem.length}};
}

Json toJson(const std::vector<thermal::ConvergenceRow>& rows) {
    Json out = Json::array();
    for (const auto& row : rows) {
        out.push_back({{"elements", row.report.elements},
                       {"h", row.report.h},
                       {"maxMidpointError", row.report.maxMidpointError},
                       {"maxNodalError", row.report.maxNodalError},
                       {"predictedMidpointError", row.report.predictedMidpointError},
                       {"observedOrder", row.observedOrder}});
    }
    return out;
}

// --------------------------------------------------------------- performance

perf::BenchmarkConfig benchmarkConfigFromJson(const Json& j) {
    perf::BenchmarkConfig c;
    c.batch.caseCount = countOr(j, "cases", c.batch.caseCount);
    c.batch.elementsPerCase = countOr(j, "elements", c.batch.elementsPerCase);
    c.batch.seed = static_cast<std::uint64_t>(countOr(j, "seed", static_cast<std::size_t>(c.batch.seed)));
    c.measuredRuns = countOr(j, "repetitions", c.measuredRuns);
    c.warmupRuns = countOr(j, "warmups", c.warmupRuns);
    if (c.measuredRuns < 1 || c.measuredRuns > 50) throw ValidationError("repetitions must be between 1 and 50");
    if (c.warmupRuns > 10) throw ValidationError("warm-up runs must be between 0 and 10");
    if (j.contains("workers")) {
        if (!j.at("workers").is_array()) throw ValidationError("'workers' must be a list of worker counts");
        c.workerCounts.clear();
        for (const auto& w : j.at("workers")) {
            if (!w.is_number_integer() || w.get<long long>() < 1 || w.get<long long>() > 256) {
                throw ValidationError("each worker count must be a whole number between 1 and 256");
            }
            c.workerCounts.push_back(static_cast<std::size_t>(w.get<long long>()));
        }
    }
    c.batch.validate();
    return c;
}

Json toJson(const perf::SystemInfo& s) {
    return {{"cpuModel", s.cpuModel},       {"physicalCores", s.physicalCores},
            {"logicalCpus", s.logicalCpus}, {"totalRamBytes", s.totalRamBytes},
            {"operatingSystem", s.operatingSystem}, {"compiler", s.compiler},
            {"buildType", s.buildType},     {"optimisationFlags", s.optimisationFlags}};
}

Json toJson(const perf::BenchmarkResult& r) {
    Json timings = Json::array();
    for (const auto& t : r.timings) {
        timings.push_back({{"workers", t.workers},
                           {"skipped", t.skipped},
                           {"skipReason", t.skipReason},
                           {"runSeconds", t.runSeconds},
                           {"medianSeconds", t.medianSeconds},
                           {"speedup", t.speedup},
                           {"efficiency", t.efficiency},
                           {"speedupVsSerial", t.speedupVsSerial},
                           {"maxAbsDiffVsSerial", t.maxAbsDiffVsSerial},
                           {"agreementPassed", t.agreementPassed}});
    }
    return {{"config",
             {{"cases", r.config.batch.caseCount},
              {"elements", r.config.batch.elementsPerCase},
              {"seed", r.config.batch.seed},
              {"kRange", {r.config.batch.conductivityMin, r.config.batch.conductivityMax}},
              {"qRange", {r.config.batch.heatGenerationMin, r.config.batch.heatGenerationMax}},
              {"warmups", r.config.warmupRuns},
              {"repetitions", r.config.measuredRuns},
              {"tolerance", r.config.agreementTolerance}}},
            {"serialRunSeconds", r.serialRunSeconds},
            {"serialMedianSeconds", r.serialMedianSeconds},
            {"timings", timings},
            {"logicalCpus", r.logicalCpus},
            {"allAgreementPassed", r.allAgreementPassed},
            {"note", r.note}};
}

// ------------------------------------------------------------------ hardware

Json toJson(const hw::DeviceDescriptor& d) {
    return {{"id", d.id},           {"kind", hw::toString(d.kind)}, {"model", d.model},
            {"hostLink", d.hostLink}, {"port", d.port},             {"usbId", d.usbId},
            {"ioVoltage", d.ioVoltage}, {"simulated", d.simulated}};
}

Json toJson(const hw::IDeviceAdapter& a) {
    Json caps = Json::array();
    for (const auto& c : a.capabilities()) caps.push_back({{"name", c.name}, {"value", c.value}});
    return {{"descriptor", toJson(a.descriptor())},
            {"state", hw::toString(a.state())},
            {"lastError", a.lastError()},
            {"capabilities", caps}};
}

Json toJson(const hw::TelemetryMessage& m) {
    Json fields = Json::array();
    for (const auto& f : m.fields) fields.push_back({{"name", f.name}, {"value", f.value}, {"unit", f.unit}});
    return {{"deviceId", m.deviceId}, {"sequence", m.sequence}, {"hostTimeMs", m.hostTimeMs},
            {"latencyMs", m.latencyMs}, {"fields", fields},     {"simulated", m.simulated}};
}

// ------------------------------------------------------------------------ ML

ml::Parameters parametersFromJson(const Json& j) {
    ml::Parameters p;
    p.w = numberOr(j, "w", p.w);
    p.b = numberOr(j, "b", p.b);
    p.v = numberOr(j, "v", p.v);
    p.c = numberOr(j, "c", p.c);
    return p;
}

Json toJson(const ml::Parameters& p) { return {{"w", p.w}, {"b", p.b}, {"v", p.v}, {"c", p.c}}; }
Json toJson(const ml::Gradients& g) { return {{"w", g.dw}, {"b", g.db}, {"v", g.dv}, {"c", g.dc}}; }

}  // namespace wb::api
