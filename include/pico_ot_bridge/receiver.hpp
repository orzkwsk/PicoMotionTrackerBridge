#pragma once

#include "pico_ot_bridge/clock_sync.hpp"
#include "pico_ot_bridge/protocol.hpp"
#include "pico_ot_bridge/udp_socket.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace pico_ot::bridge {

struct ReceiverConfig {
    uint16_t listenPort = 29765;
    int64_t timeSyncIntervalNs = 1'000'000'000LL;
    int64_t staleTimeoutNs = 500'000'000LL;
    uint64_t clientSessionId = 0;
    // Enabled by the single PC HMD ingress owner. Legacy consumers retain defaults.
    bool exclusiveBind = false;
    bool loopbackOnly = false; // Internal test route; existing C ABI/default unchanged.
    bool rejectRetiredSessions = false;
    size_t maxRetiredSessions = 1024;
    size_t maxTrackers = 256;
    size_t maxPacketsPerPump = 256;
};

struct ReceivedTrackerState {
    PosePacket pose;
    std::optional<DeviceStatePacket> deviceState;

    uint64_t senderSessionId = 0;
    uint32_t lastPoseSequence = 0;
    uint32_t lastDeviceSequence = 0;
    bool hasPoseSequence = false;
    bool hasDeviceSequence = false;

    int64_t lastPoseReceivePcNs = 0;
    int64_t lastDeviceReceivePcNs = 0;
    std::optional<int64_t> posePcMonotonicNs;
};

class Receiver {
public:
    Receiver() = default;

    bool Start(const ReceiverConfig& config, std::string* error = nullptr);
    void Stop() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept { return socket_.IsOpen(); }
    [[nodiscard]] uint16_t ListenPort() const noexcept { return socket_.BoundPort(); }
    [[nodiscard]] uint64_t ActiveSenderSessionId() const noexcept { return activeSenderSessionId_; }

    // Non-blocking. Processes at most maxPacketsPerPump datagrams, retaining only the
    // newest state per tracker serial. It also drives periodic clock sync once
    // an HMD sender endpoint has been learned.
    bool Pump(std::string* error = nullptr);

    // Optional side-channel only. Called within Pump after each decoded packet so
    // latest-only cache updates cannot hide intermediate diagnostic transitions.
    void SetDiagnosticObserver(std::function<void()> observer) { diagnosticObserver_ = std::move(observer); }

    [[nodiscard]] std::vector<std::string> Serials() const;
    [[nodiscard]] const ReceivedTrackerState* Find(const std::string& serial) const;

    [[nodiscard]] bool IsPoseStale(const ReceivedTrackerState& state,
                                   int64_t nowPcMonotonicNs) const noexcept;
    [[nodiscard]] std::optional<ClockSyncEstimate> ClockEstimate() const {
        return clockSync_.Estimate();
    }

private:
    static bool IsNewerSequence(uint32_t candidate, uint32_t previous) noexcept;
    void BeginSenderSession(uint64_t senderSessionId, const UdpEndpoint& endpoint);
    bool MaybeSendTimeSync(int64_t nowPcMonotonicNs, std::string* error);
    void HandlePacket(const DecodedPacket& decoded,
                      const UdpEndpoint& source,
                      int64_t receivePcMonotonicNs);

    ReceiverConfig config_{};
    UdpSocket socket_;
    ClockSyncEstimator clockSync_;
    std::unordered_map<std::string, ReceivedTrackerState> trackers_;
    std::unordered_set<uint64_t> retiredSenderSessions_;

    uint64_t clientSessionId_ = 0;
    uint64_t activeSenderSessionId_ = 0;
    UdpEndpoint activeSenderEndpoint_;
    uint32_t sequence_ = 0;

    uint64_t pendingTimeSyncNonce_ = 0;
    int64_t pendingTimeSyncT0_ = 0;
    int64_t lastTimeSyncSendNs_ = 0;
    std::function<void()> diagnosticObserver_;
};

}  // namespace pico_ot::bridge
