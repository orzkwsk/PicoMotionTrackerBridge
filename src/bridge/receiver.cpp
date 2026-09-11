#include "pico_ot_bridge/receiver.hpp"

#include "pico_ot_bridge/monotonic_clock.hpp"

#include <array>
#include <random>
#include <unordered_set>
#include <variant>
#include <vector>

namespace pico_ot::bridge {
namespace {

uint64_t GenerateSessionId() {
    std::random_device random;
    const uint64_t randomBits =
        (static_cast<uint64_t>(random()) << 32) ^ static_cast<uint64_t>(random());
    uint64_t value = randomBits ^
                     (static_cast<uint64_t>(MonotonicNowNs()) + 0x517cc1b727220a95ULL);
    return value == 0 ? 1 : value;
}

}  // namespace

bool Receiver::Start(const ReceiverConfig& config, std::string* error) {
    Stop();

    if (config.timeSyncIntervalNs <= 0 || config.staleTimeoutNs <= 0) {
        if (error) {
            *error = "receiver intervals must be positive";
        }
        return false;
    }

    if (!socket_.Open(config.listenPort, true, error)) {
        return false;
    }

    config_ = config;
    clientSessionId_ = config.clientSessionId != 0
                           ? config.clientSessionId
                           : GenerateSessionId();
    sequence_ = 0;
    return true;
}

void Receiver::Stop() noexcept {
    socket_.Close();
    trackers_.clear();
    clockSync_.Reset();
    activeSenderSessionId_ = 0;
    activeSenderEndpoint_ = {};
    clientSessionId_ = 0;
    sequence_ = 0;
    pendingTimeSyncNonce_ = 0;
    pendingTimeSyncT0_ = 0;
    lastTimeSyncSendNs_ = 0;
}

bool Receiver::IsNewerSequence(uint32_t candidate, uint32_t previous) noexcept {
    return static_cast<int32_t>(candidate - previous) > 0;
}

void Receiver::BeginSenderSession(uint64_t senderSessionId,
                                  const UdpEndpoint& endpoint) {
    activeSenderSessionId_ = senderSessionId;
    activeSenderEndpoint_ = endpoint;
    trackers_.clear();
    clockSync_.Reset();
    pendingTimeSyncNonce_ = 0;
    pendingTimeSyncT0_ = 0;
    lastTimeSyncSendNs_ = 0;
}

bool Receiver::MaybeSendTimeSync(int64_t nowPcMonotonicNs, std::string* error) {
    if (activeSenderSessionId_ == 0 || activeSenderEndpoint_.port == 0) {
        return true;
    }
    if (lastTimeSyncSendNs_ != 0 &&
        nowPcMonotonicNs - lastTimeSyncSendNs_ < config_.timeSyncIntervalNs) {
        return true;
    }

    TimeSyncRequestPacket request;
    pendingTimeSyncNonce_ += 1;
    if (pendingTimeSyncNonce_ == 0) {
        pendingTimeSyncNonce_ = 1;
    }
    request.nonce = pendingTimeSyncNonce_;
    request.clientSendMonotonicNs = nowPcMonotonicNs;
    pendingTimeSyncT0_ = nowPcMonotonicNs;

    std::vector<uint8_t> bytes;
    if (!EncodeTimeSyncRequestPacket(
            clientSessionId_, sequence_++, request, bytes)) {
        if (error) {
            *error = "failed to encode time-sync request";
        }
        return false;
    }
    if (!socket_.SendTo(activeSenderEndpoint_, bytes.data(), bytes.size(), error)) {
        return false;
    }

    lastTimeSyncSendNs_ = nowPcMonotonicNs;
    return true;
}

void Receiver::HandlePacket(const DecodedPacket& decoded,
                            const UdpEndpoint& source,
                            int64_t receivePcMonotonicNs) {
    // Receiver v1 supports one HMD sender at a time. A new sender session is a
    // topology lifetime boundary and invalidates all prior cached tracker state.
    if (activeSenderSessionId_ == 0) {
        BeginSenderSession(decoded.senderSessionId, source);
    } else if (decoded.senderSessionId != activeSenderSessionId_) {
        // Time-sync requests originate from a PC client and are not HMD state.
        if (std::holds_alternative<TimeSyncRequestPacket>(decoded.payload)) {
            return;
        }
        BeginSenderSession(decoded.senderSessionId, source);
    } else {
        activeSenderEndpoint_ = source;
    }

    if (const auto* pose = std::get_if<PosePacket>(&decoded.payload)) {
        auto& state = trackers_[pose->serial];
        if (state.hasPoseSequence &&
            !IsNewerSequence(decoded.sequence, state.lastPoseSequence)) {
            return;
        }

        state.pose = *pose;
        state.senderSessionId = decoded.senderSessionId;
        state.lastPoseSequence = decoded.sequence;
        state.hasPoseSequence = true;
        state.lastPoseReceivePcNs = receivePcMonotonicNs;
        state.posePcMonotonicNs.reset();
        if (pose->mappedMonotonicValid) {
            state.posePcMonotonicNs =
                clockSync_.HmdToPcMonotonicNs(pose->mappedMonotonicNs);
        }
        return;
    }

    if (const auto* device = std::get_if<DeviceStatePacket>(&decoded.payload)) {
        auto& state = trackers_[device->serial];
        if (state.hasDeviceSequence &&
            !IsNewerSequence(decoded.sequence, state.lastDeviceSequence)) {
            return;
        }

        state.deviceState = *device;
        state.senderSessionId = decoded.senderSessionId;
        state.lastDeviceSequence = decoded.sequence;
        state.hasDeviceSequence = true;
        state.lastDeviceReceivePcNs = receivePcMonotonicNs;
        return;
    }

    if (const auto* response = std::get_if<TimeSyncResponsePacket>(&decoded.payload)) {
        if (response->nonce == 0 ||
            response->nonce != pendingTimeSyncNonce_ ||
            response->clientSendMonotonicNs != pendingTimeSyncT0_) {
            return;
        }

        ClockSyncObservation observation;
        observation.clientSendMonotonicNs = response->clientSendMonotonicNs;
        observation.serverReceiveMonotonicNs = response->serverReceiveMonotonicNs;
        observation.serverSendMonotonicNs = response->serverSendMonotonicNs;
        observation.clientReceiveMonotonicNs = receivePcMonotonicNs;
        if (!clockSync_.Observe(observation)) {
            return;
        }

        for (auto& entry : trackers_) {
            auto& state = entry.second;
            if (state.hasPoseSequence && state.pose.mappedMonotonicValid) {
                state.posePcMonotonicNs =
                    clockSync_.HmdToPcMonotonicNs(state.pose.mappedMonotonicNs);
            }
        }
        return;
    }
}

bool Receiver::Pump(std::string* error) {
    if (!IsRunning()) {
        if (error) {
            *error = "receiver is not running";
        }
        return false;
    }

    std::array<uint8_t, 512> buffer{};
    for (;;) {
        size_t receivedSize = 0;
        UdpEndpoint source;
        std::string receiveError;
        const auto status = socket_.ReceiveFrom(
            buffer.data(), buffer.size(), receivedSize, source, &receiveError);

        if (status == UdpReceiveStatus::WouldBlock) {
            break;
        }
        if (status == UdpReceiveStatus::Error) {
            if (error) {
                *error = receiveError;
            }
            return false;
        }

        DecodedPacket decoded;
        if (DecodePacket(buffer.data(), receivedSize, decoded) != DecodeStatus::Ok) {
            continue;
        }
        if (decoded.senderSessionId == 0) {
            continue;
        }

        HandlePacket(decoded, source, MonotonicNowNs());
    }

    return MaybeSendTimeSync(MonotonicNowNs(), error);
}

std::vector<std::string> Receiver::Serials() const {
    std::vector<std::string> serials;
    serials.reserve(trackers_.size());
    for (const auto& entry : trackers_) {
        serials.push_back(entry.first);
    }
    return serials;
}

const ReceivedTrackerState* Receiver::Find(const std::string& serial) const {
    const auto it = trackers_.find(serial);
    return it == trackers_.end() ? nullptr : &it->second;
}

bool Receiver::IsPoseStale(const ReceivedTrackerState& state,
                           int64_t nowPcMonotonicNs) const noexcept {
    if (!state.hasPoseSequence || state.lastPoseReceivePcNs <= 0) {
        return true;
    }
    if (nowPcMonotonicNs < state.lastPoseReceivePcNs) {
        return true;
    }
    return nowPcMonotonicNs - state.lastPoseReceivePcNs > config_.staleTimeoutNs;
}

}  // namespace pico_ot::bridge
