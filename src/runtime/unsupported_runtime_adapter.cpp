#include "pico_ot_backend/runtime_adapter.hpp"

namespace pico_ot {
namespace {
class UnsupportedRuntime final : public IPicoRuntimeAdapter {
public:
    Status Start(const RuntimeConfig&) override {
        return {StatusCode::UnsupportedPlatform, 0, "PICO runtime adapter requires Android arm64-v8a"};
    }
    void Stop() override {}
    Status ReinitializeTrackerMap() override { return Start({}); }
    Status EnumerateTrackers(std::vector<DeviceInfo>&) override { return Start({}); }
    Status ReadTrackerPose(const std::string&, RawTrackerState&) override { return Start({}); }
    RuntimeDiagnostics Diagnostics() const override { return {}; }
};
}  // namespace

std::unique_ptr<IPicoRuntimeAdapter> CreateCurrentFirmwareRuntimeAdapter() {
    return std::make_unique<UnsupportedRuntime>();
}
}  // namespace pico_ot
