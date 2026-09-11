#pragma once

#include "pico_ot_backend/types.hpp"

#include <memory>
#include <string>
#include <vector>

namespace pico_ot {

struct RuntimeConfig {
    uintptr_t getBodyTrackerDataOffset = 0x47dc8;
    size_t algoResultSize = 392;
    std::string expectedLibraryBuildIdHex;
    bool allowUnverifiedRuntime = false;
    bool allowRepeatedLeakyEnumeration = false;
};

class IPicoRuntimeAdapter {
public:
    virtual ~IPicoRuntimeAdapter() = default;

    virtual Status Start(const RuntimeConfig& config) = 0;
    virtual void Stop() = 0;
    virtual Status ReinitializeTrackerMap() = 0;
    virtual Status EnumerateTrackers(std::vector<DeviceInfo>& devices) = 0;
    virtual Status ReadTrackerPose(const std::string& serial, RawTrackerState& state) = 0;
    [[nodiscard]] virtual RuntimeDiagnostics Diagnostics() const = 0;
};

std::unique_ptr<IPicoRuntimeAdapter> CreateCurrentFirmwareRuntimeAdapter();

}  // namespace pico_ot
