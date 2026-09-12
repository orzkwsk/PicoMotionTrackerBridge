#include "tracker_device.hpp"

#include "driver_log.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <utility>

namespace pico_ot::steamvr {
namespace {

vr::HmdQuaternion_t IdentityQuaternion() {
    vr::HmdQuaternion_t q{};
    q.w = 1.0;
    return q;
}

vr::HmdQuaternion_t MultiplyQuaternion(const vr::HmdQuaternion_t& a,
                                       const vr::HmdQuaternion_t& b) {
    vr::HmdQuaternion_t q{};
    q.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    q.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    q.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    q.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return q;
}

vr::HmdQuaternion_t NormalizeQuaternion(const vr::HmdQuaternion_t& q) {
    const double normSq = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
    if (normSq <= 1.0e-12) {
        return IdentityQuaternion();
    }

    const double invNorm = 1.0 / std::sqrt(normSq);
    vr::HmdQuaternion_t normalized{};
    normalized.w = q.w * invNorm;
    normalized.x = q.x * invNorm;
    normalized.y = q.y * invNorm;
    normalized.z = q.z * invNorm;
    return normalized;
}

vr::HmdQuaternion_t InverseUnitQuaternion(const vr::HmdQuaternion_t& q) {
    const vr::HmdQuaternion_t normalized = NormalizeQuaternion(q);
    vr::HmdQuaternion_t inverse{};
    inverse.w = normalized.w;
    inverse.x = -normalized.x;
    inverse.y = -normalized.y;
    inverse.z = -normalized.z;
    return inverse;
}

vr::HmdQuaternion_t PicoOutputAToOpenVrCandidate(
    const vr::HmdQuaternion_t& pico) {
    // Hardware-validated signed-axis mapping for relative orientation tests.
    // This remains intentionally explicit here until the complete absolute
    // PICO OutputA -> OpenVR world transform is calibrated.
    vr::HmdQuaternion_t openVr{};
    openVr.w = pico.w;
    openVr.x = -pico.z;
    openVr.y = pico.y;
    openVr.z = -pico.x;
    return openVr;
}

}  // namespace

TrackerDevice::TrackerDevice(std::string serial)
    : serial_(std::move(serial)), pose_(DisconnectedPose()) {
    orientationZeroReference_ = IdentityQuaternion();
}

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

void TrackerDevice::RequestOrientationZero() {
    orientationZeroPending_ = true;
    DriverLog("pico_ot_bridge: orientation zero requested for %s",
              serial_.c_str());
}

void TrackerDevice::ClearOrientationZero() {
    orientationZeroPending_ = false;
    orientationZeroActive_ = false;
    orientationZeroReference_ = IdentityQuaternion();
    DriverLog("pico_ot_bridge: orientation zero cleared for %s",
              serial_.c_str());
}

void TrackerDevice::RequestPositionZero() {
    positionZeroPending_ = true;
    DriverLog("pico_ot_bridge: position zero requested for %s",
              serial_.c_str());
}

void TrackerDevice::ClearPositionZero() {
    positionZeroPending_ = false;
    positionZeroActive_ = false;
    positionZeroReference_[0] = 0.0;
    positionZeroReference_[1] = 0.0;
    positionZeroReference_[2] = 0.0;
    DriverLog("pico_ot_bridge: position zero cleared for %s",
              serial_.c_str());
}

void TrackerDevice::SetWorldTranslation(double xMeters,
                                        double yMeters,
                                        double zMeters) noexcept {
    worldTranslationMeters_[0] = xMeters;
    worldTranslationMeters_[1] = yMeters;
    worldTranslationMeters_[2] = zMeters;
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
            if (positionZeroPending_) {
                positionZeroReference_[0] = source.positionMeters.x;
                positionZeroReference_[1] = source.positionMeters.y;
                positionZeroReference_[2] = source.positionMeters.z;
                positionZeroActive_ = true;
                positionZeroPending_ = false;
                DriverLog(
                    "pico_ot_bridge: position zero captured for %s at %.6f %.6f %.6f m",
                    serial_.c_str(),
                    positionZeroReference_[0],
                    positionZeroReference_[1],
                    positionZeroReference_[2]);
            }

            if (positionZeroActive_) {
                next.vecPosition[0] =
                    source.positionMeters.x - positionZeroReference_[0];
                next.vecPosition[1] =
                    source.positionMeters.y - positionZeroReference_[1];
                next.vecPosition[2] =
                    source.positionMeters.z - positionZeroReference_[2];
            } else {
                // Position direction and scale are hardware-validated. The
                // remaining calibration is the shared driver->world transform.
                next.vecPosition[0] = source.positionMeters.x;
                next.vecPosition[1] = source.positionMeters.y;
                next.vecPosition[2] = source.positionMeters.z;
            }

            // The position basis and meter scale are hardware-validated, so the
            // Backend linear velocity can use the same driver-space axes. This
            // is intentionally the only prediction-related change in this test;
            // angular velocity remains disabled until its frame is proven.
            next.vecVelocity[0] = source.linearVelocityMetersPerSec.x;
            next.vecVelocity[1] = source.linearVelocityMetersPerSec.y;
            next.vecVelocity[2] = source.linearVelocityMetersPerSec.z;
        }

        // Position Zero is a pure relative-motion diagnostic. Suppress the
        // shared world translation while it is active so its captured point
        // remains SteamVR origin. Normal output uses one translation shared by
        // every PICO tracker through OpenVR's driver->world transform.
        if (!positionZeroActive_) {
            next.vecWorldFromDriverTranslation[0] = worldTranslationMeters_[0];
            next.vecWorldFromDriverTranslation[1] = worldTranslationMeters_[1];
            next.vecWorldFromDriverTranslation[2] = worldTranslationMeters_[2];
        }

        if (source.orientationSamplePresent) {
            vr::HmdQuaternion_t picoOrientation{};
            picoOrientation.w = source.orientationXyzw.w;
            picoOrientation.x = source.orientationXyzw.x;
            picoOrientation.y = source.orientationXyzw.y;
            picoOrientation.z = source.orientationXyzw.z;

            const vr::HmdQuaternion_t correctedOrientation =
                NormalizeQuaternion(PicoOutputAToOpenVrCandidate(
                    picoOrientation));

            if (orientationZeroPending_) {
                orientationZeroReference_ = correctedOrientation;
                orientationZeroActive_ = true;
                orientationZeroPending_ = false;
                DriverLog("pico_ot_bridge: orientation zero captured for %s",
                          serial_.c_str());
            }

            if (orientationZeroActive_) {
                // Express the reset delta in OpenVR/world axes so the diagnostic
                // axis meaning does not rotate with the tracker's orientation at
                // the instant Zero is captured.
                next.qRotation = NormalizeQuaternion(MultiplyQuaternion(
                    correctedOrientation,
                    InverseUnitQuaternion(orientationZeroReference_)));
            } else {
                next.qRotation = correctedOrientation;
            }
        }

        // Angular velocity intentionally remains zero. The Backend value is
        // still only characterized as DeviceLocalCandidate, so passing it to
        // OpenVR before proving that frame could create incorrect prediction.

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
