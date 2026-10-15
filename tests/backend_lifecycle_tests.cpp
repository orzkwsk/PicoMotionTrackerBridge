#include "pico_ot_backend/backend.hpp"
#include "require.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace pico_ot;

namespace {
class Runtime final : public IPicoRuntimeAdapter {
public:
    std::vector<std::string> calls;
    Status startResult;
    Status enumerateResult;
    std::vector<DeviceInfo> discovered;
    bool started = false;
    bool enumerated = false;

    Status Start(const RuntimeConfig& config) override {
        REQUIRE(!config.allowRepeatedLeakyEnumeration);
        calls.push_back("start");
        started = startResult.ok();
        enumerated = false;
        return startResult;
    }
    void Stop() override { calls.push_back("stop"); started = false; }
    Status EnumerateTrackers(std::vector<DeviceInfo>& devices) override {
        REQUIRE(started);
        REQUIRE(!enumerated);
        enumerated = true;
        calls.push_back("enumerate");
        devices = discovered;
        return enumerateResult;
    }
    Status ReinitializeTrackerMap() override { calls.push_back("reinitialize"); return Status::Ok(); }
    Status ReadTrackerPose(const std::string&, RawTrackerState&) override { return Status::Ok(); }
    RuntimeDiagnostics Diagnostics() const override { return {}; }
};

void TestStartAndZeroDevices() {
    auto runtime = std::make_unique<Runtime>();
    Runtime* fake = runtime.get();
    Backend backend(std::move(runtime));
    REQUIRE(backend.Start({}).ok());
    REQUIRE((fake->calls == std::vector<std::string>{"start", "enumerate"}));
    REQUIRE(backend.Devices().empty());
    // Polling/reading the cache and reinitializing do not enumerate again.
    std::vector<TrackerState> poses;
    REQUIRE(backend.PollAll(poses).ok());
    REQUIRE(backend.ReinitializeRuntime().ok());
    REQUIRE((fake->calls == std::vector<std::string>{"start", "enumerate", "reinitialize"}));
}

void TestNonzeroAndRestart() {
    auto runtime = std::make_unique<Runtime>();
    Runtime* fake = runtime.get();
    DeviceInfo device;
    device.serial = "startup-test";
    device.connected = true;
    fake->discovered.push_back(device);
    Backend backend(std::move(runtime));
    REQUIRE(backend.Start({}).ok());
    REQUIRE(backend.Devices().size() == 1);
    REQUIRE(backend.Devices()[0].serial == device.serial);
    REQUIRE(backend.Start({}).ok());
    REQUIRE((fake->calls == std::vector<std::string>{"start", "enumerate", "stop", "start", "enumerate"}));
    backend.Stop();
    backend.Stop();
    REQUIRE(backend.Devices().empty());
    REQUIRE(backend.ReinitializeRuntime().code == StatusCode::RuntimeNotReady);
    REQUIRE(backend.Start({}).ok());
    REQUIRE(backend.Devices().size() == 1);
}

void TestFailures() {
    {
        auto runtime = std::make_unique<Runtime>();
        Runtime* fake = runtime.get();
        fake->startResult = {StatusCode::RuntimeNotReady, 0, "not ready"};
        Backend backend(std::move(runtime));
        REQUIRE(backend.Start({}).code == StatusCode::RuntimeNotReady);
        REQUIRE((fake->calls == std::vector<std::string>{"start"}));
        REQUIRE(backend.Devices().empty());
    }
    {
        auto runtime = std::make_unique<Runtime>();
        Runtime* fake = runtime.get();
        fake->enumerateResult = {StatusCode::VendorError, 7, "enumeration failed"};
        Backend backend(std::move(runtime));
        REQUIRE(backend.Start({}).code == StatusCode::VendorError);
        REQUIRE((fake->calls == std::vector<std::string>{"start", "enumerate", "stop"}));
        REQUIRE(backend.Devices().empty());
        REQUIRE(backend.RefreshDevices().code == StatusCode::RuntimeNotReady);
        fake->enumerateResult = Status::Ok();
        REQUIRE(backend.Start({}).ok());
    }
    Backend withoutRuntime(nullptr);
    REQUIRE(withoutRuntime.Start({}).code == StatusCode::RuntimeNotReady);
}
}

int main() {
    TestStartAndZeroDevices();
    TestNonzeroAndRestart();
    TestFailures();
    std::cout << "Backend lifecycle: sequence, failures, zero/nonzero, restart, single enumeration PASS\n";
}
