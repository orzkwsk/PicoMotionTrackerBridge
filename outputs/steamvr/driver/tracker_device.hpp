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

    // Diagnostic-only orientation zeroing. RequestOrientationZero captures the
    // next fresh corrected orientation as the reference so the displayed
    // orientation becomes identity at that instant. Position is unaffected.
    void RequestOrientationZero();
    void ClearOrientationZero();

    // Diagnostic-only position zeroing. RequestPositionZero captures the next
    // fresh valid position and subtracts it from subsequent samples. This is
    // intentionally per-tracker so relative motion can be validated without a
    // guessed world-space translation.
    void RequestPositionZero();
    void ClearPositionZero();

    void Update(const pico_ot::bridge::ReceivedTrackerState* state,
                bool stale,
                int64_t nowPcMonotonicNs);

private:
    static vr::DriverPose_t DisconnectedPose();

    std::string serial_;
    vr::TrackedDeviceIndex_t objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    vr::DriverPose_t pose_{};

    bool orientationZeroPending_ = false;
    bool orientationZeroActive_ = false;
    vr::HmdQuaternion_t orientationZeroReference_{};

    bool positionZeroPending_ = false;
    bool positionZeroActive_ = false;
    double positionZeroReference_[3] = {0.0, 0.0, 0.0};
};

}  // namespace pico_ot::steamvr
