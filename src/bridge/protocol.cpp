#include "pico_ot_bridge/protocol.hpp"

#include <cmath>
#include <cstring>
#include <limits>
#include <type_traits>

namespace pico_ot::bridge {
namespace {

constexpr uint32_t kFlagConnectedSnapshot = 1u << 0;
constexpr uint32_t kFlagPositionValid = 1u << 1;
constexpr uint32_t kFlagOrientationPresent = 1u << 2;
constexpr uint32_t kFlagMappedMonotonicValid = 1u << 3;

constexpr uint32_t kDeviceFlagConnectedSnapshot = 1u << 0;
constexpr uint32_t kDeviceFlagCharging = 1u << 1;
constexpr uint32_t kDeviceFlagBatteryPresent = 1u << 2;

constexpr size_t kPosePayloadSize = 174;
constexpr size_t kDeviceStatePayloadSize = 49;
constexpr size_t kTimeSyncRequestPayloadSize = 16;
constexpr size_t kTimeSyncResponsePayloadSize = 32;

template <typename T>
using UnsignedT = std::make_unsigned_t<T>;

template <typename T>
void AppendIntegralLE(std::vector<uint8_t>& out, T value) {
    static_assert(std::is_integral_v<T>, "integral required");
    using U = UnsignedT<T>;
    const U u = static_cast<U>(value);
    for (size_t i = 0; i < sizeof(T); ++i) {
        out.push_back(static_cast<uint8_t>((u >> (i * 8)) & static_cast<U>(0xff)));
    }
}

void AppendDoubleLE(std::vector<uint8_t>& out, double value) {
    static_assert(sizeof(double) == sizeof(uint64_t), "wire protocol requires IEEE-754 binary64");
    uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    AppendIntegralLE(out, bits);
}

bool AppendSerial(std::vector<uint8_t>& out, const std::string& serial) {
    if (serial.empty() || serial.size() >= kWireSerialCapacity) {
        return false;
    }
    const size_t base = out.size();
    out.resize(base + kWireSerialCapacity, 0);
    std::memcpy(out.data() + base, serial.data(), serial.size());
    return true;
}

void AppendHeader(std::vector<uint8_t>& out,
                  WireMessageType type,
                  uint32_t sequence,
                  uint32_t payloadSize) {
    AppendIntegralLE(out, kWireMagic);
    AppendIntegralLE(out, kWireVersion);
    AppendIntegralLE(out, static_cast<uint16_t>(type));
    AppendIntegralLE(out, sequence);
    AppendIntegralLE(out, payloadSize);
}

class Reader {
public:
    Reader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

    template <typename T>
    bool ReadIntegral(T& out) {
        static_assert(std::is_integral_v<T>, "integral required");
        if (remaining() < sizeof(T)) {
            return false;
        }
        using U = UnsignedT<T>;
        U value = 0;
        for (size_t i = 0; i < sizeof(T); ++i) {
            value |= static_cast<U>(data_[offset_ + i]) << (i * 8);
        }
        offset_ += sizeof(T);
        out = static_cast<T>(value);
        return true;
    }

    bool ReadDouble(double& out) {
        uint64_t bits = 0;
        if (!ReadIntegral(bits)) {
            return false;
        }
        std::memcpy(&out, &bits, sizeof(out));
        return true;
    }

    bool ReadSerial(std::string& out) {
        if (remaining() < kWireSerialCapacity) {
            return false;
        }
        const char* begin = reinterpret_cast<const char*>(data_ + offset_);
        size_t length = 0;
        while (length < kWireSerialCapacity && begin[length] != '\0') {
            ++length;
        }
        offset_ += kWireSerialCapacity;
        if (length == 0 || length == kWireSerialCapacity) {
            return false;
        }
        out.assign(begin, length);
        return true;
    }

    bool Skip(size_t count) {
        if (remaining() < count) {
            return false;
        }
        offset_ += count;
        return true;
    }

    [[nodiscard]] size_t remaining() const noexcept { return size_ - offset_; }

private:
    const uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t offset_ = 0;
};

bool Finite(const Vec3d& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool Finite(const Quatd& q) {
    return std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z) && std::isfinite(q.w);
}

bool ValidPoseNumerics(const PosePacket& packet) {
    return Finite(packet.positionMeters) &&
           Finite(packet.orientationXyzw) &&
           Finite(packet.linearVelocityMetersPerSec) &&
           Finite(packet.angularVelocityRadPerSec);
}

bool EncodePosePayload(const PosePacket& packet, std::vector<uint8_t>& payload) {
    if (!ValidPoseNumerics(packet) || !AppendSerial(payload, packet.serial)) {
        return false;
    }

    AppendIntegralLE(payload, packet.runtimeDeviceId);

    uint32_t flags = 0;
    flags |= packet.connectedSnapshot ? kFlagConnectedSnapshot : 0;
    flags |= packet.positionValid ? kFlagPositionValid : 0;
    flags |= packet.orientationSamplePresent ? kFlagOrientationPresent : 0;
    flags |= packet.mappedMonotonicValid ? kFlagMappedMonotonicValid : 0;
    AppendIntegralLE(payload, flags);

    AppendIntegralLE(payload, packet.rawTrackingState65);
    payload.insert(payload.end(), 3, 0);  // Reserved for protocol-v1 flag expansion.

    AppendIntegralLE(payload, packet.sourceTimestamp);
    AppendIntegralLE(payload, packet.observedMonotonicNs);
    AppendIntegralLE(payload, packet.mappedMonotonicNs);

    AppendDoubleLE(payload, packet.positionMeters.x);
    AppendDoubleLE(payload, packet.positionMeters.y);
    AppendDoubleLE(payload, packet.positionMeters.z);

    AppendDoubleLE(payload, packet.orientationXyzw.x);
    AppendDoubleLE(payload, packet.orientationXyzw.y);
    AppendDoubleLE(payload, packet.orientationXyzw.z);
    AppendDoubleLE(payload, packet.orientationXyzw.w);

    AppendDoubleLE(payload, packet.linearVelocityMetersPerSec.x);
    AppendDoubleLE(payload, packet.linearVelocityMetersPerSec.y);
    AppendDoubleLE(payload, packet.linearVelocityMetersPerSec.z);

    AppendDoubleLE(payload, packet.angularVelocityRadPerSec.x);
    AppendDoubleLE(payload, packet.angularVelocityRadPerSec.y);
    AppendDoubleLE(payload, packet.angularVelocityRadPerSec.z);

    AppendIntegralLE(payload, packet.referenceFrame);
    AppendIntegralLE(payload, packet.angularVelocityFrame);
    return payload.size() == kPosePayloadSize;
}

bool DecodePosePayload(Reader& reader, PosePacket& packet) {
    uint32_t flags = 0;
    if (!reader.ReadSerial(packet.serial) ||
        !reader.ReadIntegral(packet.runtimeDeviceId) ||
        !reader.ReadIntegral(flags) ||
        !reader.ReadIntegral(packet.rawTrackingState65) ||
        !reader.Skip(3) ||
        !reader.ReadIntegral(packet.sourceTimestamp) ||
        !reader.ReadIntegral(packet.observedMonotonicNs) ||
        !reader.ReadIntegral(packet.mappedMonotonicNs) ||
        !reader.ReadDouble(packet.positionMeters.x) ||
        !reader.ReadDouble(packet.positionMeters.y) ||
        !reader.ReadDouble(packet.positionMeters.z) ||
        !reader.ReadDouble(packet.orientationXyzw.x) ||
        !reader.ReadDouble(packet.orientationXyzw.y) ||
        !reader.ReadDouble(packet.orientationXyzw.z) ||
        !reader.ReadDouble(packet.orientationXyzw.w) ||
        !reader.ReadDouble(packet.linearVelocityMetersPerSec.x) ||
        !reader.ReadDouble(packet.linearVelocityMetersPerSec.y) ||
        !reader.ReadDouble(packet.linearVelocityMetersPerSec.z) ||
        !reader.ReadDouble(packet.angularVelocityRadPerSec.x) ||
        !reader.ReadDouble(packet.angularVelocityRadPerSec.y) ||
        !reader.ReadDouble(packet.angularVelocityRadPerSec.z) ||
        !reader.ReadIntegral(packet.referenceFrame) ||
        !reader.ReadIntegral(packet.angularVelocityFrame)) {
        return false;
    }

    packet.connectedSnapshot = (flags & kFlagConnectedSnapshot) != 0;
    packet.positionValid = (flags & kFlagPositionValid) != 0;
    packet.orientationSamplePresent = (flags & kFlagOrientationPresent) != 0;
    packet.mappedMonotonicValid = (flags & kFlagMappedMonotonicValid) != 0;
    return ValidPoseNumerics(packet);
}

bool EncodeDeviceStatePayload(const DeviceStatePacket& packet,
                              std::vector<uint8_t>& payload) {
    if (!AppendSerial(payload, packet.serial)) {
        return false;
    }
    AppendIntegralLE(payload, packet.runtimeDeviceId);

    uint32_t flags = 0;
    flags |= packet.connectedSnapshot ? kDeviceFlagConnectedSnapshot : 0;
    flags |= packet.charging ? kDeviceFlagCharging : 0;
    flags |= packet.batteryBucketPresent ? kDeviceFlagBatteryPresent : 0;
    AppendIntegralLE(payload, flags);
    AppendIntegralLE(payload, packet.batteryBucketRaw);
    AppendIntegralLE(payload, packet.observedMonotonicNs);
    return payload.size() == kDeviceStatePayloadSize;
}

bool DecodeDeviceStatePayload(Reader& reader, DeviceStatePacket& packet) {
    uint32_t flags = 0;
    if (!reader.ReadSerial(packet.serial) ||
        !reader.ReadIntegral(packet.runtimeDeviceId) ||
        !reader.ReadIntegral(flags) ||
        !reader.ReadIntegral(packet.batteryBucketRaw) ||
        !reader.ReadIntegral(packet.observedMonotonicNs)) {
        return false;
    }
    packet.connectedSnapshot = (flags & kDeviceFlagConnectedSnapshot) != 0;
    packet.charging = (flags & kDeviceFlagCharging) != 0;
    packet.batteryBucketPresent = (flags & kDeviceFlagBatteryPresent) != 0;
    return true;
}

template <typename Payload, typename EncodePayload>
bool EncodePacket(WireMessageType type,
                  uint32_t sequence,
                  const Payload& packet,
                  size_t expectedPayloadSize,
                  EncodePayload encodePayload,
                  std::vector<uint8_t>& out) {
    std::vector<uint8_t> payload;
    payload.reserve(expectedPayloadSize);
    if (!encodePayload(packet, payload) || payload.size() != expectedPayloadSize) {
        return false;
    }

    out.clear();
    out.reserve(kWireHeaderSize + payload.size());
    AppendHeader(out, type, sequence, static_cast<uint32_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
    return true;
}

}  // namespace

bool EncodePosePacket(uint32_t sequence,
                      const PosePacket& packet,
                      std::vector<uint8_t>& out) {
    return EncodePacket(WireMessageType::Pose,
                        sequence,
                        packet,
                        kPosePayloadSize,
                        EncodePosePayload,
                        out);
}

bool EncodeDeviceStatePacket(uint32_t sequence,
                             const DeviceStatePacket& packet,
                             std::vector<uint8_t>& out) {
    return EncodePacket(WireMessageType::DeviceState,
                        sequence,
                        packet,
                        kDeviceStatePayloadSize,
                        EncodeDeviceStatePayload,
                        out);
}

bool EncodeTimeSyncRequestPacket(uint32_t sequence,
                                 const TimeSyncRequestPacket& packet,
                                 std::vector<uint8_t>& out) {
    const auto encoder = [](const TimeSyncRequestPacket& p, std::vector<uint8_t>& payload) {
        AppendIntegralLE(payload, p.nonce);
        AppendIntegralLE(payload, p.clientSendMonotonicNs);
        return true;
    };
    return EncodePacket(WireMessageType::TimeSyncRequest,
                        sequence,
                        packet,
                        kTimeSyncRequestPayloadSize,
                        encoder,
                        out);
}

bool EncodeTimeSyncResponsePacket(uint32_t sequence,
                                  const TimeSyncResponsePacket& packet,
                                  std::vector<uint8_t>& out) {
    const auto encoder = [](const TimeSyncResponsePacket& p, std::vector<uint8_t>& payload) {
        AppendIntegralLE(payload, p.nonce);
        AppendIntegralLE(payload, p.clientSendMonotonicNs);
        AppendIntegralLE(payload, p.serverReceiveMonotonicNs);
        AppendIntegralLE(payload, p.serverSendMonotonicNs);
        return true;
    };
    return EncodePacket(WireMessageType::TimeSyncResponse,
                        sequence,
                        packet,
                        kTimeSyncResponsePayloadSize,
                        encoder,
                        out);
}

DecodeStatus DecodePacket(const uint8_t* data,
                          size_t size,
                          DecodedPacket& out) {
    if (data == nullptr || size < kWireHeaderSize) {
        return DecodeStatus::Truncated;
    }

    Reader reader(data, size);
    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t typeRaw = 0;
    uint32_t sequence = 0;
    uint32_t payloadSize = 0;
    if (!reader.ReadIntegral(magic) ||
        !reader.ReadIntegral(version) ||
        !reader.ReadIntegral(typeRaw) ||
        !reader.ReadIntegral(sequence) ||
        !reader.ReadIntegral(payloadSize)) {
        return DecodeStatus::Truncated;
    }

    if (magic != kWireMagic) {
        return DecodeStatus::BadMagic;
    }
    if (version != kWireVersion) {
        return DecodeStatus::UnsupportedVersion;
    }
    if (reader.remaining() != payloadSize) {
        return DecodeStatus::InvalidPayloadSize;
    }

    const auto type = static_cast<WireMessageType>(typeRaw);
    const auto requireSize = [payloadSize](size_t expected) {
        return payloadSize == expected;
    };

    switch (type) {
        case WireMessageType::Pose: {
            if (!requireSize(kPosePayloadSize)) {
                return DecodeStatus::InvalidPayloadSize;
            }
            PosePacket packet;
            if (!DecodePosePayload(reader, packet)) {
                return reader.remaining() == 0 ? DecodeStatus::InvalidNumericValue
                                               : DecodeStatus::InvalidSerial;
            }
            if (reader.remaining() != 0) {
                return DecodeStatus::InvalidPayloadSize;
            }
            out.sequence = sequence;
            out.payload = std::move(packet);
            return DecodeStatus::Ok;
        }
        case WireMessageType::DeviceState: {
            if (!requireSize(kDeviceStatePayloadSize)) {
                return DecodeStatus::InvalidPayloadSize;
            }
            DeviceStatePacket packet;
            if (!DecodeDeviceStatePayload(reader, packet)) {
                return DecodeStatus::InvalidSerial;
            }
            if (reader.remaining() != 0) {
                return DecodeStatus::InvalidPayloadSize;
            }
            out.sequence = sequence;
            out.payload = std::move(packet);
            return DecodeStatus::Ok;
        }
        case WireMessageType::TimeSyncRequest: {
            if (!requireSize(kTimeSyncRequestPayloadSize)) {
                return DecodeStatus::InvalidPayloadSize;
            }
            TimeSyncRequestPacket packet;
            if (!reader.ReadIntegral(packet.nonce) ||
                !reader.ReadIntegral(packet.clientSendMonotonicNs) ||
                reader.remaining() != 0) {
                return DecodeStatus::Truncated;
            }
            out.sequence = sequence;
            out.payload = packet;
            return DecodeStatus::Ok;
        }
        case WireMessageType::TimeSyncResponse: {
            if (!requireSize(kTimeSyncResponsePayloadSize)) {
                return DecodeStatus::InvalidPayloadSize;
            }
            TimeSyncResponsePacket packet;
            if (!reader.ReadIntegral(packet.nonce) ||
                !reader.ReadIntegral(packet.clientSendMonotonicNs) ||
                !reader.ReadIntegral(packet.serverReceiveMonotonicNs) ||
                !reader.ReadIntegral(packet.serverSendMonotonicNs) ||
                reader.remaining() != 0) {
                return DecodeStatus::Truncated;
            }
            out.sequence = sequence;
            out.payload = packet;
            return DecodeStatus::Ok;
        }
        default:
            return DecodeStatus::UnsupportedType;
    }
}

const char* DecodeStatusString(DecodeStatus status) noexcept {
    switch (status) {
        case DecodeStatus::Ok: return "ok";
        case DecodeStatus::Truncated: return "truncated";
        case DecodeStatus::BadMagic: return "bad_magic";
        case DecodeStatus::UnsupportedVersion: return "unsupported_version";
        case DecodeStatus::UnsupportedType: return "unsupported_type";
        case DecodeStatus::InvalidPayloadSize: return "invalid_payload_size";
        case DecodeStatus::InvalidSerial: return "invalid_serial";
        case DecodeStatus::InvalidNumericValue: return "invalid_numeric_value";
    }
    return "unknown";
}

}  // namespace pico_ot::bridge
