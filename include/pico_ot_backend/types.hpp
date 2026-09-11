#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pico_ot {

enum class StatusCode : int32_t {
    Ok = 0,
    InvalidArgument,
    UnsupportedPlatform,
    LibraryOpenFailed,
    SymbolMissing,
    RuntimeAbiMismatch,
    RuntimeBuildMismatch,
    RuntimeNotReady,
    VendorError,
    TrackerNotFound,
    EnumerationRefreshDisabled,
    CorruptVendorOutput,
};

struct Status {
    StatusCode code = StatusCode::Ok;
    int32_t vendorCode = 0;
    std::string message;

    [[nodiscard]] bool ok() const noexcept { return code == StatusCode::Ok; }
    static Status Ok() { return {}; }
};

struct Vec3d {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Quatd {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double w = 1.0;
};

enum class ReferenceFrame : uint8_t {
    PicoOutputA = 0,
};

enum class AngularVelocityFrame : uint8_t {
    Unknown = 0,
    DeviceLocalCandidate = 1,
};

struct RuntimeDiagnostics {
    std::string libraryPath;
    std::string libraryBuildIdHex;
    uintptr_t libraryBase = 0;
    uintptr_t getBodyTrackerDataAddress = 0;
    uintptr_t getBodyTrackerDataOffset = 0;
    uint64_t intentionallyLeakedEnumerationBytes = 0;
    bool runtimeIdentityVerified = false;
};

struct DeviceInfo {
    std::string serial;
    int32_t runtimeDeviceId = -1;
    bool connected = false;
    bool charging = false;
    std::optional<uint8_t> batteryBucketRaw;
    int64_t observedMonotonicNs = 0;
};

struct RawTrackerState {
    std::string serial;
    int32_t runtimeDeviceId = -1;
    bool connectedSnapshot = false;

    uint64_t sourceTimestamp = 0;
    int64_t observedMonotonicNs = 0;
    int64_t mappedMonotonicNs = 0;
    bool mappedMonotonicValid = false;

    bool positionValid = false;
    bool orientationSamplePresent = false;
    uint8_t rawTrackingState65 = 0;
    int32_t vendorReturnCode = 0;

    Vec3d positionMm;
    Quatd orientationXyzw;
    Vec3d linearVelocityMmPerSec;
    Vec3d linearAccelerationRaw;
    Vec3d angularVelocityRawRadPerSec;

    ReferenceFrame frame = ReferenceFrame::PicoOutputA;
    AngularVelocityFrame angularVelocityFrame = AngularVelocityFrame::DeviceLocalCandidate;
};

struct TrackerState {
    std::string serial;
    int32_t runtimeDeviceId = -1;
    bool connectedSnapshot = false;

    uint64_t sourceTimestamp = 0;
    int64_t observedMonotonicNs = 0;
    int64_t mappedMonotonicNs = 0;
    bool mappedMonotonicValid = false;

    bool positionValid = false;
    bool orientationSamplePresent = false;
    uint8_t rawTrackingState65 = 0;
    int32_t vendorReturnCode = 0;

    Vec3d positionMeters;
    Quatd orientationXyzw;
    Vec3d linearVelocityMetersPerSec;
    std::optional<Vec3d> linearAccelerationMetersPerSec2;
    Vec3d angularVelocityRadPerSec;

    ReferenceFrame frame = ReferenceFrame::PicoOutputA;
    AngularVelocityFrame angularVelocityFrame = AngularVelocityFrame::DeviceLocalCandidate;
};

}  // namespace pico_ot
