// Mock (simulated) implementations of the three workbench devices.
//
// The mocks model a request/response link with a latency. A healthy link
// answers in a few milliseconds; an injected fault makes the simulated
// device stay silent, so the host waits for its full deadline and raises
// DeviceTimeoutError - the same failure path a real unplugged cable or hung
// firmware would exercise.
#include "workbench/core/errors.hpp"
#include "workbench/hardware/device.hpp"

#include <cmath>
#include <random>
#include <thread>

namespace wb::hw {
namespace {

using Clock = std::chrono::steady_clock;

double hostTimeMs() {
    static const auto origin = Clock::now();
    return std::chrono::duration<double, std::milli>(Clock::now() - origin).count();
}

/// Shared state machine and simulated link for all mock devices.
class MockDeviceBase : public IDeviceAdapter {
public:
    MockDeviceBase(DeviceDescriptor d, std::uint64_t seed) : desc_(std::move(d)), rng_(seed) {}

    const DeviceDescriptor& descriptor() const noexcept override { return desc_; }

    ConnectionState state() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return state_;
    }

    std::string lastError() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return lastError_;
    }

    void connect(std::chrono::milliseconds timeout) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ConnectionState::Connected) return;  // idempotent
        exchange(timeout, FaultMode::ConnectTimeout, "connect handshake");
        state_ = ConnectionState::Connected;
        lastError_.clear();
        sequence_ = 0;
    }

    void disconnect() override {
        std::lock_guard<std::mutex> lock(mutex_);
        state_ = ConnectionState::Disconnected;
    }

    TelemetryMessage requestTelemetry(std::chrono::milliseconds timeout) override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == ConnectionState::Faulted) {
            throw DeviceStateError(desc_.id + " is faulted; run Recover before requesting telemetry");
        }
        if (state_ != ConnectionState::Connected) {
            throw DeviceStateError(desc_.id + " is not connected");
        }
        TelemetryMessage msg;
        msg.latencyMs = exchange(timeout, FaultMode::TelemetryTimeout, "telemetry request");
        msg.deviceId = desc_.id;
        msg.sequence = ++sequence_;
        msg.hostTimeMs = hostTimeMs();
        msg.fields = sampleTelemetry();
        return msg;
    }

    void injectFault(FaultMode mode) override {
        std::lock_guard<std::mutex> lock(mutex_);
        armedFault_ = mode;
    }

protected:
    /// Gaussian noise for plausible-looking (clearly simulated) readings.
    double noise(double sigma) { return std::normal_distribution<double>(0.0, sigma)(rng_); }
    double secondsConnected() const { return static_cast<double>(sequence_) * 1.0; }
    std::uint64_t sequence() const { return sequence_; }
    virtual std::vector<TelemetryField> sampleTelemetry() = 0;

private:
    /// Simulated round trip. Returns latency in ms or throws on timeout.
    double exchange(std::chrono::milliseconds timeout, FaultMode failureForThisOp, const char* what) {
        const bool fail = (armedFault_ == failureForThisOp);
        if (fail) armedFault_ = FaultMode::None;  // one-shot
        if (fail) {
            // The device never answers: the host blocks for its whole deadline.
            std::this_thread::sleep_for(timeout);
            state_ = ConnectionState::Faulted;
            lastError_ = std::string(what) + " timed out after " + std::to_string(timeout.count()) +
                         " ms (simulated fault)";
            throw DeviceTimeoutError(desc_.id + ": " + lastError_);
        }
        const double latency = 2.0 + std::uniform_real_distribution<double>(0.0, 6.0)(rng_);
        std::this_thread::sleep_for(std::chrono::microseconds(static_cast<long>(latency * 1000)));
        return latency;
    }

    DeviceDescriptor desc_;
    mutable std::mutex mutex_;
    ConnectionState state_ = ConnectionState::Disconnected;
    FaultMode armedFault_ = FaultMode::None;
    std::string lastError_;
    std::uint64_t sequence_ = 0;
    std::mt19937_64 rng_;
};

// ----------------------------------------------------------- microcontroller

class MockMicrocontroller final : public MockDeviceBase {
public:
    explicit MockMicrocontroller(const std::string& id)
        : MockDeviceBase({id, DeviceKind::Microcontroller, "ST NUCLEO-F446RE (STM32F446RE, Cortex-M4F) - assumed",
                          "USB 2.0 FS -> on-board ST-LINK/V2-1 virtual COM port, 115200 8N1", "SIM-COM3",
                          "0483:374B (ST-LINK/V2-1)", "3.3 V GPIO (selected pins 5 V tolerant)", true},
                         0xA11CE) {}

    std::vector<Capability> capabilities() const override {
        return {{"Core", "Arm Cortex-M4F @ 180 MHz"},
                {"Host link", "UART over ST-LINK VCP, 115200 baud, 8N1"},
                {"I/O voltage", "3.3 V; do not drive 5 V into non-FT pins"},
                {"ADC", "12-bit, 0-3.3 V reference"},
                {"Debug port", "SWD/JTAG on CN4 / Morpho (on-board ST-LINK owns it unless CN2 jumpers removed)"},
                {"Telemetry", "die temperature, VDDA, ADC channel, heartbeat"}};
    }

protected:
    std::vector<TelemetryField> sampleTelemetry() override {
        return {{"die_temperature", 34.0 + noise(0.3), "C"},
                {"vdda", 3.30 + noise(0.005), "V"},
                {"adc_ch0", 1.65 + 0.2 * std::sin(0.3 * static_cast<double>(sequence())) + noise(0.002), "V"},
                {"heartbeat", static_cast<double>(sequence()), "count"}};
    }
};

// ---------------------------------------------------------------------- FPGA

class MockFpga final : public MockDeviceBase {
public:
    explicit MockFpga(const std::string& id)
        : MockDeviceBase({id, DeviceKind::Fpga, "Digilent Basys 3 (Xilinx Artix-7 XC7A35T) - assumed",
                          "USB 2.0 -> on-board FTDI FT2232HQ: channel A JTAG, channel B UART 115200",
                          "SIM-COM4", "0403:6010 (FTDI FT2232H)", "3.3 V LVCMOS33 user I/O banks", true},
                         0xF96A) {}

    std::vector<Capability> capabilities() const override {
        return {{"Device", "XC7A35T-1CPG236C, 33 280 logic cells"},
                {"Host link", "UART over FT2232 channel B (design-defined protocol)"},
                {"Configuration", "on-board USB-JTAG (FT2232 channel A) or QSPI flash"},
                {"I/O voltage", "3.3 V Pmod headers; VCCINT 1.0 V internal"},
                {"Monitoring", "XADC die temperature and supply rails"},
                {"Telemetry", "XADC temperature, VCCINT, VCCAUX, MMCM lock, design ID"}};
    }

protected:
    std::vector<TelemetryField> sampleTelemetry() override {
        return {{"xadc_temperature", 41.5 + noise(0.4), "C"},
                {"vccint", 1.000 + noise(0.004), "V"},
                {"vccaux", 1.800 + noise(0.006), "V"},
                {"mmcm_locked", 1.0, "bool"},
                {"design_id", static_cast<double>(0x00C0FFEE), "hex"}};
    }
};

// --------------------------------------------------------------------- JTAG

class MockJtagDebugger final : public MockDeviceBase {
public:
    explicit MockJtagDebugger(const std::string& id)
        : MockDeviceBase({id, DeviceKind::JtagDebugger, "SEGGER J-Link-class JTAG/SWD probe - assumed",
                          "USB 2.0 HS, vendor protocol (GDB server on host)", "SIM-USB-JLINK",
                          "1366:0101 (SEGGER J-Link)", "follows target VTref (1.2-5.0 V)", true},
                         0x7A6) {}

    std::vector<Capability> capabilities() const override {
        return {{"Role", "Debug/programming interface - not a compute target"},
                {"Protocols", "JTAG (IEEE 1149.1) and Arm SWD"},
                {"Connector", "20-pin 0.1\" Arm JTAG; adapter to 10-pin 1.27 mm Cortex Debug"},
                {"Level translation", "on-probe buffers referenced to target VTref pin"},
                {"Signal ownership", "probe drives TCK/TMS/TDI/nTRST; target drives TDO"},
                {"Telemetry", "VTref, TCK frequency, target IDCODE, scan-chain length"}};
    }

protected:
    std::vector<TelemetryField> sampleTelemetry() override {
        return {{"vtref", 3.29 + noise(0.004), "V"},
                {"tck_frequency", 4000.0, "kHz"},
                {"target_idcode", static_cast<double>(0x4BA00477u), "hex"},  // Arm JTAG-DP (Cortex-M4)
                {"scan_chain_devices", 1.0, "count"}};
    }
};

}  // namespace

std::unique_ptr<IDeviceAdapter> makeMockMicrocontroller(const std::string& id) {
    return std::make_unique<MockMicrocontroller>(id);
}
std::unique_ptr<IDeviceAdapter> makeMockFpga(const std::string& id) { return std::make_unique<MockFpga>(id); }
std::unique_ptr<IDeviceAdapter> makeMockJtagDebugger(const std::string& id) {
    return std::make_unique<MockJtagDebugger>(id);
}

}  // namespace wb::hw
