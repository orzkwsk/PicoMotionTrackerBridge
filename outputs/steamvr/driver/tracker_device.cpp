#include "tracker_device.hpp"

#include "driver_log.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace pico_ot::steamvr {
namespace {

vr::HmdQuaternion_t IdentityQuaternion() {
    vr::HmdQuaternion_t q{};
    q.w = 1.0;
    return q;
}

}  // namespace

TrackerDevice::TrackerDevice(std::string serial)
    : serial_(std::move(serial)), pose_(DisconnectedPose()) {}

vr::EVRInitError TrackerDevice::Activate(uint32_t objectId) {
    objectId_ = objectId;

    const vr::PropertyContainerHandle_t container =
        vr::VRProperties()->TrackedDeviceToPropertyContainer(objectId_);
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_SerialNumber_String, serial_.c_str());
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_ModelNumber_String, "PICO Motion Tracker Bridge");
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_ManufacturerName_String, "PICO");
    vr::VRProperties()->SetStringProperty(
        container, vr::Prop_TrackingSystemName_String, "pico_ot_bridge");
    vr::VRProperties()->SetBoolProperty(
        container, vr::Prop_DeviceIsWireless_Bool, true);
    // Backend battery semantics are still a raw bucket candidate, so do not
    // advertise SteamVR percentage battery support yet.
    vr::VRProperties()->SetBoolProperty(
        container, vr::Prop_DeviceProvidesBatteryStatus_Bool, false);

    DriverLog("pico_ot_bridge: activated tracker %s as device %u",
              serial_.c_str(), objectId_);
    return vr::VRInitError_None;
}

void TrackerDevice::Deactivate() {
    objectId_ = vr::k_unTrackedDeviceIndexInvalid;
    pose_ = DisconnectedPose();
}

void TrackerDevice::EnterStandby() {}

void* TrackerDevice::GetComponent(const char* /*componentNameAndVersion*/) {
    return nullptr;
}

void TrackerDevice::DebugRequest(const char* /*request*/,
                                 char* responseBuffer,
                                 uint32_t responseBufferSize) {
    if (responseBuffer != nullptr && responseBufferSize > 0) {
        responseBuffer[0] = '\0';
    }
}

vr::DriverPose_t TrackerDevice::GetPose() {
    return pose_;
}

vr::DriverPose_t TrackerDevice::DisconnectedPose() {
    vr::DriverPose_t pose{};
    pose.qWorldFromDriverRotation = IdentityQuaternion();
    pose.qDriverFromHeadRotation = IdentityQuaternion();
    pose.qRotation = IdentityQuaternion();
    pose.deviceIsConnected = false;
    pose.poseIsValid = false;
    pose.result = vr::TrackingResult_Uninitialized;
    return pose;
}

void TrackerDevice::Update(const pico_ot::bridge::ReceivedTrackerState* state,
                           bool stale,
                           int64_t nowPcMonotonicNs) {
    vr::DriverPose_t next = DisconnectedPose();

    if (state != nullptr && state->hasPoseSequence && !stale) {
        next.deviceIsConnected = true;

        const auto& source = state->pose;
        const bool fullPoseValid = source.positionValid &&
                                   source.orientationSamplePresent;
        next.poseIsValid = fullPoseValid;
        next.result = fullPoseValid
                          ? vr::TrackingResult_Running_OK
                          : vr::TrackingResult_Running_OutOfRange;

        if (source.positionValid) {
            // Diagnostic bring-up path only. PicoOutputA -> OpenVR basis
            // calibration is intentionally not guessed here.
            next.vecPosition[0] = source.positionMeters.x;
            next.vecPosition[1] = source.positionMeters.y;
            next.vecPosition[2] = source.positionMeters.z;
        }

        if (source.orientationSamplePresent) {
            next.qRotation.w = source.orientationXyzw.w;
            next.qRotation.x = source.orientationXyzw.x;
            next.qRotation.y = source.orientationXyzw.y;
            next.qRotation.z = source.orientationXyzw.z;
        }

        // Velocity fields intentionally remain zero for initial bring-up.
        // The PICO/OpenVR basis is not calibrated yet, and angular velocity is
        // only proven to be a device/local-frame candidate.

        if (state->posePcMonotonicNs.has_value() && nowPcMonotonicNs > 0) {
            const int64_t deltaNs = *state->posePcMonotonicNs - nowPcMonotonicNs;
            // Keep a corrupted/old clock estimate from causing extreme runtime
            // prediction. Real values during normal transport are small/negative.
            const double seconds = static_cast<double>(deltaNs) / 1.0e9;
            next.poseTimeOffset = std::clamp(seconds, -0.100, 0.020);
        }
    }

    pose_ = next;
    if (objectId_ != vr::k_unTrackedDeviceIndexInvalid) {
        vr::VRServerDriverHost()->TrackedDevicePoseUpdated(
            objectId_, pose_, sizeof(vr::DriverPose_t));
    }
}

}  // namespace pico_ot::steamvr
