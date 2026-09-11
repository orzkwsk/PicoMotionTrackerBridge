#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace pico_ot::bridge {

constexpr uint32_t kWireMagic = 0x42544F50u;  // "POTB" when encoded little-endian.
constexpr uint16_t kWireVersion = 1;
constexpr size_t kWireSerialCapacity = 32;
constexpr size_t kWireHeaderSize = 24;

enum class WireMessageType : uint16_t {
    Pose = 1,
    DeviceState = 2,
    TimeSyncRequest = 3,
    TimeSyncResponse = 4,
};

enum class DecodeStatus : uint8_t {
    Ok = 0,
    Truncated,
    BadMagic,
    UnsupportedVersion,
    UnsupportedType,
    InvalidPayloadSize,
    InvalidSerial,
    InvalidNumericValue,
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

struct PosePacket {
    std::string serial;
    int32_t runtimeDeviceId = -1;

    bool connectedSnapshot = false;
    bool positionValid = false;
    bool orientationSamplePresent = false;
    bool mappedMonotonicValid = false;
    uint8_t rawTrackingState65 = 0;

    uint64_t sourceTimestamp = 0;
    int64_t observedMonotonicNs = 0;
    int64_t mappedMonotonicNs = 0;

    Vec3d positionMeters;
    Quatd orientationXyzw;
    Vec3d linearVelocityMetersPerSec;
    Vec3d angularVelocityRadPerSec;

    uint8_t referenceFrame = 0;          // Mirrors pico_ot::ReferenceFrame.
    uint8_t angularVelocityFrame = 0;    // Mirrors pico_ot::AngularVelocityFrame.
};

struct DeviceStatePacket {
    std::string serial;
    int32_t runtimeDeviceId = -1;
    bool connectedSnapshot = false;
    bool charging = false;
    bool batteryBucketPresent = false;
    uint8_t batteryBucketRaw = 0;
    int64_t observedMonotonicNs = 0;
};

// NTP-style clock-domain mapping. The PC sends t0. The HMD records t1 on
// receive and t2 immediately before send. The PC records t3 on receive and
// can estimate the HMD<->PC monotonic offset without mixing it into Backend.
struct TimeSyncRequestPacket {
    uint64_t nonce = 0;
    int64_t clientSendMonotonicNs = 0;  // t0, PC clock.
};

struct TimeSyncResponsePacket {
    uint64_t nonce = 0;
    int64_t clientSendMonotonicNs = 0;    // t0, echoed PC clock.
    int64_t serverReceiveMonotonicNs = 0; // t1, HMD clock.
    int64_t serverSendMonotonicNs = 0;    // t2, HMD clock.
};

using PacketPayload = std::variant<PosePacket,
                                   DeviceStatePacket,
                                   TimeSyncRequestPacket,
                                   TimeSyncResponsePacket>;

struct DecodedPacket {
    // Random/non-repeating value generated when the HMD sender starts. A
    // change tells the PC receiver that packet sequence numbers and topology
    // belong to a new sender lifetime.
    uint64_t senderSessionId = 0;
    uint32_t sequence = 0;
    PacketPayload payload;
};

[[nodiscard]] bool EncodePosePacket(uint64_t senderSessionId,
                                    uint32_t sequence,
                                    const PosePacket& packet,
                                    std::vector<uint8_t>& out);
[[nodiscard]] bool EncodeDeviceStatePacket(uint64_t senderSessionId,
                                           uint32_t sequence,
                                           const DeviceStatePacket& packet,
                                           std::vector<uint8_t>& out);
[[nodiscard]] bool EncodeTimeSyncRequestPacket(uint64_t senderSessionId,
                                               uint32_t sequence,
                                               const TimeSyncRequestPacket& packet,
                                               std::vector<uint8_t>& out);
[[nodiscard]] bool EncodeTimeSyncResponsePacket(uint64_t senderSessionId,
                                                uint32_t sequence,
                                                const TimeSyncResponsePacket& packet,
                                                std::vector<uint8_t>& out);

[[nodiscard]] DecodeStatus DecodePacket(const uint8_t* data,
                                        size_t size,
                                        DecodedPacket& out);

[[nodiscard]] const char* DecodeStatusString(DecodeStatus status) noexcept;

}  // namespace pico_ot::bridge
