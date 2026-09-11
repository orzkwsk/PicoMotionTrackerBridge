#pragma once

#include "pico_ot_bridge/receiver.hpp"

#include <openvr_driver.h>

#include <cstdint>
#include <string>

namespace pico_ot::steamvr {

class TrackerDevice final : public vr::ITrackedDeviceServerDriver {
public:
    explicit TrackerDevice(std::string serial);

    vr::EVRInitError Activate(uint32_t objectId) override;
    void Deactivate() override;
    void EnterStandby() override;
    void* GetComponent(const char* componentNameAndVersion) override;
    void DebugRequest(const char* request,
                      char* responseBuffer,
                      uint32_t responseBufferSize) override;
    vr::DriverPose_t GetPose() override;

    [[nodiscard]] const std::string& Serial() const noexcept { return serial_; }

    void Update(const pico_ot::bridge::ReceivedTrackerState* state,
                bool stale,
                int64_t nowPcMonotonicNs);

private:
    static vr::DriverPose_t DisconnectedPose();

    std::string serial_;
    vr::TrackedDeviceIndex_t objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    vr::DriverPose_t pose_{};
};

}  // namespace pico_ot::steamvr
