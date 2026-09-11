#include "pico_ot_bridge/publisher.hpp"

#include "pico_ot_bridge/monotonic_clock.hpp"

#include <array>
#include <random>
#include <variant>
#include <vector>

namespace pico_ot::bridge {
namespace {

uint64_t GenerateSessionId() {
    std::random_device random;
    const uint64_t randomBits =
        (static_cast<uint64_t>(random()) << 32) ^ static_cast<uint64_t>(random());
    const uint64_t timeBits = static_cast<uint64_t>(MonotonicNowNs());
    uint64_t value = randomBits ^ (timeBits + 0x9e3779b97f4a7c15ULL);
    if (value == 0) {
        value = 1;
    }
    return value;
}

PosePacket ToPacket(const pico_ot::TrackerState& state) {
    PosePacket packet;
    packet.serial = state.serial;
    packet.runtimeDeviceId = state.runtimeDeviceId;
    packet.connectedSnapshot = state.connectedSnapshot;
    packet.positionValid = state.positionValid;
    packet.orientationSamplePresent = state.orientationSamplePresent;
    packet.mappedMonotonicValid = state.mappedMonotonicValid;
    packet.rawTrackingState65 = state.rawTrackingState65;
    packet.sourceTimestamp = state.sourceTimestamp;
    packet.observedMonotonicNs = state.observedMonotonicNs;
    packet.mappedMonotonicNs = state.mappedMonotonicNs;
    packet.positionMeters = {
        state.positionMeters.x,
        state.positionMeters.y,
        state.positionMeters.z,
    };
    packet.orientationXyzw = {
        state.orientationXyzw.x,
        state.orientationXyzw.y,
        state.orientationXyzw.z,
        state.orientationXyzw.w,
    };
    packet.linearVelocityMetersPerSec = {
        state.linearVelocityMetersPerSec.x,
        state.linearVelocityMetersPerSec.y,
        state.linearVelocityMetersPerSec.z,
    };
    packet.angularVelocityRadPerSec = {
        state.angularVelocityRadPerSec.x,
        state.angularVelocityRadPerSec.y,
        state.angularVelocityRadPerSec.z,
    };
    packet.referenceFrame = static_cast<uint8_t>(state.frame);
    packet.angularVelocityFrame = static_cast<uint8_t>(state.angularVelocityFrame);
    return packet;
}

DeviceStatePacket ToPacket(const pico_ot::DeviceInfo& device) {
    DeviceStatePacket packet;
    packet.serial = device.serial;
    packet.runtimeDeviceId = device.runtimeDeviceId;
    packet.connectedSnapshot = device.connected;
    packet.charging = device.charging;
    packet.batteryBucketPresent = device.batteryBucketRaw.has_value();
    packet.batteryBucketRaw = device.batteryBucketRaw.value_or(0);
    packet.observedMonotonicNs = device.observedMonotonicNs;
    return packet;
}

}  // namespace

bool Publisher::Start(const PublisherConfig& config, std::string* error) {
    Stop();

    if (config.pcEndpoint.address.empty() || config.pcEndpoint.port == 0) {
        if (error) {
            *error = "publisher PC endpoint is not configured";
        }
        return false;
    }

    if (!socket_.Open(config.localPort, true, error)) {
        return false;
    }

    pcEndpoint_ = config.pcEndpoint;
    senderSessionId_ = config.senderSessionId != 0
                           ? config.senderSessionId
                           : GenerateSessionId();
    sequence_ = 0;
    return true;
}

void Publisher::Stop() noexcept {
    socket_.Close();
    pcEndpoint_ = {};
    senderSessionId_ = 0;
    sequence_ = 0;
}

bool Publisher::SendPose(const pico_ot::TrackerState& state, std::string* error) {
    if (!IsRunning()) {
        if (error) {
            *error = "publisher is not running";
        }
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!EncodePosePacket(senderSessionId_, NextSequence(), ToPacket(state), bytes)) {
        if (error) {
            *error = "failed to encode pose packet";
        }
        return false;
    }
    return socket_.SendTo(pcEndpoint_, bytes.data(), bytes.size(), error);
}

bool Publisher::SendDeviceState(const pico_ot::DeviceInfo& device, std::string* error) {
    if (!IsRunning()) {
        if (error) {
            *error = "publisher is not running";
        }
        return false;
    }

    std::vector<uint8_t> bytes;
    if (!EncodeDeviceStatePacket(senderSessionId_, NextSequence(), ToPacket(device), bytes)) {
        if (error) {
            *error = "failed to encode device-state packet";
        }
        return false;
    }
    return socket_.SendTo(pcEndpoint_, bytes.data(), bytes.size(), error);
}

bool Publisher::PumpControl(std::string* error) {
    if (!IsRunning()) {
        if (error) {
            *error = "publisher is not running";
        }
        return false;
    }

    std::array<uint8_t, 512> buffer{};
    for (;;) {
        size_t receivedSize = 0;
        UdpEndpoint sender;
        std::string receiveError;
        const UdpReceiveStatus status = socket_.ReceiveFrom(
            buffer.data(), buffer.size(), receivedSize, sender, &receiveError);

        if (status == UdpReceiveStatus::WouldBlock) {
            return true;
        }
        if (status == UdpReceiveStatus::Error) {
            if (error) {
                *error = receiveError;
            }
            return false;
        }

        const int64_t receiveNs = MonotonicNowNs();
        DecodedPacket decoded;
        if (DecodePacket(buffer.data(), receivedSize, decoded) != DecodeStatus::Ok ||
            !std::holds_alternative<TimeSyncRequestPacket>(decoded.payload)) {
            continue;
        }

        const auto& request = std::get<TimeSyncRequestPacket>(decoded.payload);
        TimeSyncResponsePacket response;
        response.nonce = request.nonce;
        response.clientSendMonotonicNs = request.clientSendMonotonicNs;
        response.serverReceiveMonotonicNs = receiveNs;
        response.serverSendMonotonicNs = MonotonicNowNs();

        std::vector<uint8_t> bytes;
        if (!EncodeTimeSyncResponsePacket(
                senderSessionId_, NextSequence(), response, bytes)) {
            continue;
        }

        std::string sendError;
        if (!socket_.SendTo(sender, bytes.data(), bytes.size(), &sendError)) {
            if (error) {
                *error = sendError;
            }
            return false;
        }
    }
}

}  // namespace pico_ot::bridge
