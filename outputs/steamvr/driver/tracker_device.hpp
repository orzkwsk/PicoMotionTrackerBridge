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

    // Common PICO-driver-space -> SteamVR-world translation. Every PICO tracker
    // receives the same value; per-device mount offsets do not belong here.
    void SetWorldTranslation(double xMeters,
                             double yMeters,
                             double zMeters) noexcept;

    // Switch between the visible PICO-like model and a microscopic hidden
    // render model. Keeping a valid render-model property avoids SteamVR's
    // generic-tracker fallback when the user hides the custom model.
    void SetRenderModelVisible(bool visible);

    void Update(const pico_ot::bridge::ReceivedTrackerState* state,
                bool stale,
                int64_t nowPcMonotonicNs);

private:
    static vr::DriverPose_t DisconnectedPose();
    void ApplyRenderModelProperty();

    std::string serial_;
    vr::TrackedDeviceIndex_t objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    vr::DriverPose_t pose_{};

    bool orientationZeroPending_ = false;
    bool orientationZeroActive_ = false;
    vr::HmdQuaternion_t orientationZeroReference_{};

    bool positionZeroPending_ = false;
    bool positionZeroActive_ = false;
    double positionZeroReference_[3] = {0.0, 0.0, 0.0};

    double worldTranslationMeters_[3] = {0.0, 0.0, 0.0};
    bool renderModelVisible_ = true;

    // Experimental OpenVR angular prediction state. This is derived from the
    // already-corrected/output quaternion rather than the Backend's provisional
    // device-local angular-velocity field.
    bool angularVelocityHistoryValid_ = false;
    uint64_t angularVelocitySenderSessionId_ = 0;
    uint32_t angularVelocityPoseSequence_ = 0;
    int64_t angularVelocityPoseTimeNs_ = 0;
    vr::HmdQuaternion_t angularVelocityOrientation_{};
    double angularVelocityRadPerSec_[3] = {0.0, 0.0, 0.0};
};

}  // namespace pico_ot::steamvr
