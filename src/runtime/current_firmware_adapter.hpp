#pragma once

#include "pico_ot_backend/runtime_adapter.hpp"

namespace pico_ot {

class CurrentFirmwareRuntimeAdapter final : public IPicoRuntimeAdapter {
public:
    CurrentFirmwareRuntimeAdapter();
    ~CurrentFirmwareRuntimeAdapter() override;

    Status Start(const RuntimeConfig& config) override;
    void Stop() override;
    Status ReinitializeTrackerMap() override;
    Status EnumerateTrackers(std::vector<DeviceInfo>& devices) override;
    Status ReadTrackerPose(const std::string& serial, RawTrackerState& state) override;
    [[nodiscard]] RuntimeDiagnostics Diagnostics() const override;

private:
    struct Impl;
    Impl* impl_;
};

}  // namespace pico_ot
