#include "workbench/core/errors.hpp"
#include "workbench/hardware/device.hpp"

#include <thread>

namespace wb::hw {

const char* toString(DeviceKind kind) noexcept {
    switch (kind) {
        case DeviceKind::Microcontroller: return "microcontroller";
        case DeviceKind::Fpga: return "fpga";
        case DeviceKind::JtagDebugger: return "jtag_debugger";
    }
    return "unknown";
}

const char* toString(ConnectionState state) noexcept {
    switch (state) {
        case ConnectionState::Disconnected: return "disconnected";
        case ConnectionState::Connected: return "connected";
        case ConnectionState::Faulted: return "faulted";
    }
    return "unknown";
}

const char* toString(FaultMode mode) noexcept {
    switch (mode) {
        case FaultMode::None: return "none";
        case FaultMode::TelemetryTimeout: return "telemetry_timeout";
        case FaultMode::ConnectTimeout: return "connect_timeout";
    }
    return "unknown";
}

FaultMode parseFaultMode(const std::string& text) {
    if (text == "none") return FaultMode::None;
    if (text == "telemetry_timeout") return FaultMode::TelemetryTimeout;
    if (text == "connect_timeout") return FaultMode::ConnectTimeout;
    throw ValidationError("unknown fault mode '" + text + "' (use none, telemetry_timeout or connect_timeout)");
}

std::vector<DeviceDescriptor> DeviceManager::discover() {
    std::lock_guard<std::mutex> lock(mutex_);
    adapters_.clear();
    adapters_.push_back(makeMockMicrocontroller("mcu0"));
    adapters_.push_back(makeMockFpga("fpga0"));
    adapters_.push_back(makeMockJtagDebugger("jtag0"));
    std::vector<DeviceDescriptor> found;
    for (const auto& a : adapters_) found.push_back(a->descriptor());
    return found;
}

std::vector<std::shared_ptr<IDeviceAdapter>> DeviceManager::devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return adapters_;
}

std::shared_ptr<IDeviceAdapter> DeviceManager::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& a : adapters_) {
        if (a->descriptor().id == id) return a;
    }
    throw ValidationError("no device with id '" + id + "'; run discovery first");
}

DeviceManager::RecoveryReport DeviceManager::recover(const std::string& id, int maxAttempts) {
    if (maxAttempts < 1) throw ValidationError("recovery needs at least one attempt");
    const auto device = find(id);  // manager lock is not held during I/O
    RecoveryReport report;
    device->disconnect();
    report.log.push_back("link reset (disconnect)");
    auto backoff = std::chrono::milliseconds(50);
    for (int attempt = 1; attempt <= maxAttempts; ++attempt) {
        report.attempts = attempt;
        try {
            device->connect(kDefaultTimeout);
            report.recovered = true;
            report.log.push_back("attempt " + std::to_string(attempt) + ": connected");
            return report;
        } catch (const DeviceTimeoutError& e) {
            report.log.push_back("attempt " + std::to_string(attempt) + ": " + e.what());
            std::this_thread::sleep_for(backoff);
            backoff *= 2;
        }
    }
    report.log.push_back("giving up after " + std::to_string(maxAttempts) + " attempts");
    return report;
}

}  // namespace wb::hw
