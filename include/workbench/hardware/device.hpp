// Hardware abstraction layer for the host-connected embedded workbench.
//
// IMPORTANT: every adapter in this build is a SIMULATION (mock). No USB, UART
// or JTAG traffic is generated. The interface is shaped so a real transport
// (serial port, libusb, vendor DLL) could implement IDeviceAdapter later
// without changing the UI or transport layers.
#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace wb::hw {

enum class DeviceKind { Microcontroller, Fpga, JtagDebugger };
enum class ConnectionState { Disconnected, Connected, Faulted };
enum class FaultMode { None, TelemetryTimeout, ConnectTimeout };

const char* toString(DeviceKind kind) noexcept;
const char* toString(ConnectionState state) noexcept;
const char* toString(FaultMode mode) noexcept;
FaultMode parseFaultMode(const std::string& text);  // throws wb::ValidationError

/// Recoverable: the device did not answer within the host's deadline.
class DeviceTimeoutError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

/// Operation is not valid in the current state (e.g. telemetry while disconnected).
class DeviceStateError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DeviceDescriptor {
    std::string id;           // stable workbench id, e.g. "mcu0"
    DeviceKind kind = DeviceKind::Microcontroller;
    std::string model;        // board assumption, e.g. "ST NUCLEO-F446RE (assumed)"
    std::string hostLink;     // how it would reach the host, e.g. "USB CDC virtual COM"
    std::string port;         // simulated enumeration, e.g. "SIM-COM3"
    std::string usbId;        // VID:PID the real board would enumerate as
    std::string ioVoltage;    // e.g. "3.3 V LVCMOS"
    bool simulated = true;
};

struct Capability {
    std::string name;
    std::string value;
};

struct TelemetryField {
    std::string name;
    double value = 0.0;
    std::string unit;
};

struct TelemetryMessage {
    std::string deviceId;
    std::uint64_t sequence = 0;
    double hostTimeMs = 0.0;   // host steady-clock time when received
    double latencyMs = 0.0;    // simulated round-trip latency
    std::vector<TelemetryField> fields;
    bool simulated = true;
};

class IDeviceAdapter {
public:
    virtual ~IDeviceAdapter() = default;
    virtual const DeviceDescriptor& descriptor() const noexcept = 0;
    virtual std::vector<Capability> capabilities() const = 0;
    virtual ConnectionState state() const = 0;
    virtual std::string lastError() const = 0;
    /// Throws DeviceTimeoutError (state -> Faulted) if no answer within timeout.
    virtual void connect(std::chrono::milliseconds timeout) = 0;
    virtual void disconnect() = 0;
    /// Throws DeviceStateError if not connected, DeviceTimeoutError on timeout.
    virtual TelemetryMessage requestTelemetry(std::chrono::milliseconds timeout) = 0;
    /// Arms a one-shot simulated failure for the next matching operation.
    virtual void injectFault(FaultMode mode) = 0;
};

/// Owns the adapters; thread-safe because HTTP requests arrive concurrently.
class DeviceManager {
public:
    /// Simulated discovery: (re)creates the three mock adapters.
    std::vector<DeviceDescriptor> discover();
    std::vector<std::shared_ptr<IDeviceAdapter>> devices() const;
    std::shared_ptr<IDeviceAdapter> find(const std::string& id) const;  // throws ValidationError

    struct RecoveryReport {
        bool recovered = false;
        int attempts = 0;
        std::vector<std::string> log;
    };
    /// Disconnect, then reconnect with bounded retries and backoff.
    RecoveryReport recover(const std::string& id, int maxAttempts = 3);

    static constexpr std::chrono::milliseconds kDefaultTimeout{250};

private:
    mutable std::mutex mutex_;
    std::vector<std::shared_ptr<IDeviceAdapter>> adapters_;
};

std::unique_ptr<IDeviceAdapter> makeMockMicrocontroller(const std::string& id);
std::unique_ptr<IDeviceAdapter> makeMockFpga(const std::string& id);
std::unique_ptr<IDeviceAdapter> makeMockJtagDebugger(const std::string& id);

}  // namespace wb::hw
