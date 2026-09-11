#pragma once

#include "pico_ot_backend/runtime_adapter.hpp"
#include "pico_ot_backend/types.hpp"

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pico_ot {

struct BackendConfig {
    RuntimeConfig runtime;
    size_t clockWindowSamples = 9;
    int64_t clockDiscontinuityThresholdNs = 100'000'000LL;
};

class Backend {
public:
    explicit Backend(std::unique_ptr<IPicoRuntimeAdapter> runtime = CreateCurrentFirmwareRuntimeAdapter());
    ~Backend();

    Backend(const Backend&) = delete;
    Backend& operator=(const Backend&) = delete;

    Status Start(const BackendConfig& config);
    void Stop();
    Status RefreshDevices();
    Status ReinitializeRuntime();

    [[nodiscard]] std::vector<DeviceInfo> Devices() const;
    [[nodiscard]] RuntimeDiagnostics Diagnostics() const;

    Status PollRaw(const std::string& serial, RawTrackerState& out);
    Status Poll(const std::string& serial, TrackerState& out);
    Status PollAll(std::vector<TrackerState>& out);

private:
    class ClockMapper;

    TrackerState Normalize(const RawTrackerState& raw) const;
    void MergeDevices(const std::vector<DeviceInfo>& devices);
    DeviceInfo LookupDeviceSnapshot(const std::string& serial) const;

    mutable std::mutex mutex_;
    std::unique_ptr<IPicoRuntimeAdapter> runtime_;
    std::unique_ptr<ClockMapper> clockMapper_;
    BackendConfig config_{};
    bool started_ = false;
    std::unordered_map<std::string, DeviceInfo> devices_;
};

}  // namespace pico_ot
