#pragma once

#include "pico_ot_backend/types.hpp"

#include <cstddef>
#include <cstdint>

namespace pico_ot::internal {

constexpr size_t kObservedAlgoResultSize = 392;
constexpr size_t kOutputBufferSize = 512;
constexpr size_t kPositionValidOffset = 64;
constexpr size_t kTrackingState65Offset = 65;
constexpr size_t kLinearVelocityOffset = 224;
constexpr size_t kLinearAccelerationOffset = 248;
constexpr size_t kAngularVelocityOffset = 272;

Status DecodeOutputA(
    const uint8_t* data,
    size_t dataSize,
    int32_t vendorReturnCode,
    int64_t observedMonotonicNs,
    RawTrackerState& out);

bool OutputTailUnmodified(const uint8_t* data, size_t observedSize, size_t bufferSize, uint8_t canary);

}  // namespace pico_ot::internal
