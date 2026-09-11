#pragma once

#include "pico_ot_bridge/receiver.hpp"
#include "tracker_device.hpp"

#include <openvr_driver.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace pico_ot::steamvr {

class DeviceProvider final : public vr::IServerTrackedDeviceProvider {
public:
    vr::EVRInitError Init(vr::IVRDriverContext* driverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override;
    void EnterStandby() override;
    void LeaveStandby() override;

private:
    void AddNewTrackers();
    void UpdateTrackers(int64_t nowPcMonotonicNs);

    pico_ot::bridge::Receiver receiver_;
    std::unordered_map<std::string, std::unique_ptr<TrackerDevice>> devices_;
    bool receiverErrorLogged_ = false;
};

}  // namespace pico_ot::steamvr
