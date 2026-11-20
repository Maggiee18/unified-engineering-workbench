#include "workbench/core/errors.hpp"
#include "workbench/hardware/device.hpp"

#include <doctest.h>

#include <chrono>

using namespace wb;
using namespace wb::hw;
using namespace std::chrono_literals;

// Enum comparisons use CHECK((a == b)): doctest would otherwise try to
// stringify the enums via the ADL-visible hw::toString(), which returns const char*.

TEST_CASE("Discovery finds one device of each kind, all labelled simulated") {
    DeviceManager mgr;
    const auto found = mgr.discover();
    REQUIRE(found.size() == 3);
    CHECK((found[0].kind == DeviceKind::Microcontroller));
    CHECK((found[1].kind == DeviceKind::Fpga));
    CHECK((found[2].kind == DeviceKind::JtagDebugger));
    for (const auto& d : found) {
        CHECK(d.simulated);
        CHECK_FALSE(mgr.find(d.id)->capabilities().empty());
    }
}

TEST_CASE("Connect, telemetry and disconnect for all three devices") {
    DeviceManager mgr;
    mgr.discover();
    for (const auto& dev : mgr.devices()) {
        CHECK((dev->state() == ConnectionState::Disconnected));
        dev->connect(250ms);
        CHECK((dev->state() == ConnectionState::Connected));
        const TelemetryMessage m1 = dev->requestTelemetry(250ms);
        const TelemetryMessage m2 = dev->requestTelemetry(250ms);
        CHECK(m1.simulated);
        CHECK(m2.sequence == m1.sequence + 1);
        CHECK_FALSE(m1.fields.empty());
        dev->disconnect();
        CHECK((dev->state() == ConnectionState::Disconnected));
    }
}

TEST_CASE("Telemetry while disconnected is a state error") {
    DeviceManager mgr;
    mgr.discover();
    CHECK_THROWS_AS(mgr.find("mcu0")->requestTelemetry(100ms), DeviceStateError);
}

TEST_CASE("Injected telemetry timeout faults the device; recovery restores it") {
    DeviceManager mgr;
    mgr.discover();
    auto fpga = mgr.find("fpga0");
    fpga->connect(250ms);
    fpga->injectFault(FaultMode::TelemetryTimeout);
    const auto start = std::chrono::steady_clock::now();
    CHECK_THROWS_AS(fpga->requestTelemetry(80ms), DeviceTimeoutError);
    CHECK(std::chrono::steady_clock::now() - start >= 80ms);  // host really waited its deadline
    CHECK((fpga->state() == ConnectionState::Faulted));
    CHECK(fpga->lastError().find("timed out") != std::string::npos);
    CHECK_THROWS_AS(fpga->requestTelemetry(80ms), DeviceStateError);  // must recover first

    const auto report = mgr.recover("fpga0");
    CHECK(report.recovered);
    CHECK(report.attempts == 1);
    CHECK((fpga->state() == ConnectionState::Connected));
    CHECK_NOTHROW(fpga->requestTelemetry(250ms));
}

TEST_CASE("Connect timeout is retried by recovery") {
    DeviceManager mgr;
    mgr.discover();
    auto jtag = mgr.find("jtag0");
    jtag->injectFault(FaultMode::ConnectTimeout);
    // The one-shot fault is consumed by recovery's first attempt; the second succeeds.
    const auto report = mgr.recover("jtag0");
    CHECK(report.recovered);
    CHECK(report.attempts == 2);
}

TEST_CASE("Unknown device and fault names are validation errors") {
    DeviceManager mgr;
    mgr.discover();
    CHECK_THROWS_AS(mgr.find("gpu9"), ValidationError);
    CHECK_THROWS_AS(parseFaultMode("meltdown"), ValidationError);
    CHECK((parseFaultMode("telemetry_timeout") == FaultMode::TelemetryTimeout));
}
