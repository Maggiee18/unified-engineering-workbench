// Conversion between core result types and JSON (transport layer only).
// Keeping this out of the core means the numerical code has no dependency
// on the HTTP/JSON stack.
#pragma once

#include "workbench/geometry/stl_mesh.hpp"
#include "workbench/hardware/device.hpp"
#include "workbench/ml/backprop.hpp"
#include "workbench/performance/batch.hpp"
#include "workbench/performance/system_info.hpp"
#include "workbench/thermal/nodal_csv.hpp"
#include "workbench/thermal/rod_fea.hpp"

#include <json.hpp>

namespace wb::api {

using Json = nlohmann::json;

/// Flattened Float32-friendly arrays (positions/normals) for WebGL.
Json toJson(const geometry::StlMesh& mesh);
Json toJson(const thermal::NodalDataset& data);

/// Reads RodProblem fields from JSON; missing keys keep assignment defaults.
/// Throws wb::ValidationError for non-numeric values.
thermal::RodProblem rodProblemFromJson(const Json& j);
Json toJson(const thermal::RodProblem& p);
/// Includes nodal field, dense exact curve (samplesPerElement per element) and midpoint errors.
Json toJson(const thermal::RodSolution& s, const thermal::MidpointErrorReport& r, std::size_t exactSamples = 400);
Json toJson(const std::vector<thermal::ConvergenceRow>& rows);

perf::BenchmarkConfig benchmarkConfigFromJson(const Json& j);
Json toJson(const perf::BenchmarkResult& r);
Json toJson(const perf::SystemInfo& s);

Json toJson(const hw::DeviceDescriptor& d);
Json toJson(const hw::IDeviceAdapter& a);  // descriptor + state + capabilities
Json toJson(const hw::TelemetryMessage& m);

ml::Parameters parametersFromJson(const Json& j);
Json toJson(const ml::Parameters& p);
Json toJson(const ml::Gradients& g);

/// Helper: number from JSON with default; rejects non-numbers and non-finite.
double numberOr(const Json& j, const char* key, double fallback);
std::size_t countOr(const Json& j, const char* key, std::size_t fallback);

}  // namespace wb::api
