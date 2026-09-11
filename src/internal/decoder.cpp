#include "internal/decoder.hpp"

#include <cmath>
#include <cstring>

namespace pico_ot::internal {
namespace {

template <typename T>
bool ReadAt(const uint8_t* data, size_t dataSize, size_t offset, T& out) {
    if (data == nullptr || offset + sizeof(T) > dataSize) return false;
    std::memcpy(&out, data + offset, sizeof(T));
    return true;
}

bool ReadVec3(const uint8_t* data, size_t dataSize, size_t offset, Vec3d& out) {
    return ReadAt(data, dataSize, offset + 0, out.x) &&
           ReadAt(data, dataSize, offset + 8, out.y) &&
           ReadAt(data, dataSize, offset + 16, out.z);
}

bool IsFinite(const Vec3d& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool IsFinite(const Quatd& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
}

}  // namespace

Status DecodeOutputA(
    const uint8_t* data,
    size_t dataSize,
    int32_t vendorReturnCode,
    int64_t observedMonotonicNs,
    RawTrackerState& out) {
    if (data == nullptr || dataSize < kObservedAlgoResultSize) {
        return {StatusCode::InvalidArgument, vendorReturnCode, "algo_result_tracker buffer is too small"};
    }

    out.vendorReturnCode = vendorReturnCode;
    out.observedMonotonicNs = observedMonotonicNs;

    if (!ReadAt(data, dataSize, 0, out.sourceTimestamp) ||
        !ReadVec3(data, dataSize, 8, out.positionMm) ||
        !ReadAt(data, dataSize, 32, out.orientationXyzw.x) ||
        !ReadAt(data, dataSize, 40, out.orientationXyzw.y) ||
        !ReadAt(data, dataSize, 48, out.orientationXyzw.z) ||
        !ReadAt(data, dataSize, 56, out.orientationXyzw.w) ||
        !ReadVec3(data, dataSize, kLinearVelocityOffset, out.linearVelocityMmPerSec) ||
        !ReadVec3(data, dataSize, kLinearAccelerationOffset, out.linearAccelerationRaw) ||
        !ReadVec3(data, dataSize, kAngularVelocityOffset, out.angularVelocityRawRadPerSec)) {
        return {StatusCode::RuntimeAbiMismatch, vendorReturnCode, "failed to decode known algo_result_tracker fields"};
    }

    out.positionValid = data[kPositionValidOffset] != 0;
    out.rawTrackingState65 = data[kTrackingState65Offset];

    const double norm2 = out.orientationXyzw.x * out.orientationXyzw.x +
                         out.orientationXyzw.y * out.orientationXyzw.y +
                         out.orientationXyzw.z * out.orientationXyzw.z +
                         out.orientationXyzw.w * out.orientationXyzw.w;
    out.orientationSamplePresent = IsFinite(out.orientationXyzw) && norm2 > 0.25 && norm2 < 2.25;

    if (!IsFinite(out.positionMm) || !IsFinite(out.linearVelocityMmPerSec) ||
        !IsFinite(out.linearAccelerationRaw) || !IsFinite(out.angularVelocityRawRadPerSec)) {
        return {StatusCode::CorruptVendorOutput, vendorReturnCode, "non-finite value in decoded vendor output"};
    }

    return Status::Ok();
}

bool OutputTailUnmodified(const uint8_t* data, size_t observedSize, size_t bufferSize, uint8_t canary) {
    if (data == nullptr || observedSize > bufferSize) return false;
    for (size_t i = observedSize; i < bufferSize; ++i) {
        if (data[i] != canary) return false;
    }
    return true;
}

}  // namespace pico_ot::internal
