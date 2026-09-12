#pragma once

#include "pico_ot_backend/types.hpp"
#include "pico_ot_bridge/protocol.hpp"
#include "pico_ot_bridge/udp_socket.hpp"

#include <cstdint>
#include <string>

namespace pico_ot::bridge {

struct PublisherConfig {
    UdpEndpoint pcEndpoint;
    uint16_t localPort = 0;
    uint64_t senderSessionId = 0;
};

class Publisher {
public:
    Publisher() = default;

    bool Start(const PublisherConfig& config, std::string* error = nullptr);
    void Stop() noexcept;

    [[nodiscard]] bool IsRunning() const noexcept { return socket_.IsOpen(); }
    [[nodiscard]] uint16_t LocalPort() const noexcept { return socket_.BoundPort(); }
    [[nodiscard]] uint64_t SenderSessionId() const noexcept { return senderSessionId_; }

    bool SendPose(const pico_ot::TrackerState& state, std::string* error = nullptr);
    bool SendDeviceState(const pico_ot::DeviceInfo& device, std::string* error = nullptr);

    // Drains non-blocking control traffic. Currently this services
    // TimeSyncRequest packets and sends TimeSyncResponse packets back to the
    // request source. Returns false only for a socket-level error.
    bool PumpControl(std::string* error = nullptr);

private:
    [[nodiscard]] uint32_t NextSequence() noexcept { return sequence_++; }

    UdpSocket socket_;
    UdpEndpoint pcEndpoint_;
    uint64_t senderSessionId_ = 0;
    uint32_t sequence_ = 0;
};

}  // namespace pico_ot::bridge
