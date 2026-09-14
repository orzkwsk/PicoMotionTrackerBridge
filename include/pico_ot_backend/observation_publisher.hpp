#pragma once
#include "pico_ot_backend/observation_adapter.hpp"
#include "pico_ot_bridge/udp_socket.hpp"
namespace pico_ot {
// No receive loop, no ingress bind, no unbounded retry queue. A stopped C1
// consumer cannot block the existing POTB fan-out paths.
class ObservationPublisher {
public:
    bool Start(uint16_t port = 29810, std::string* error = nullptr);
    void Publish(PicoObservationAdapter& adapter, const bridge::Receiver& receiver, int64_t nowPcNs);
    [[nodiscard]] uint64_t Sent() const noexcept { return sent_; }
    [[nodiscard]] uint64_t Errors() const noexcept { return errors_; }
private:
    bridge::UdpSocket socket_;
    bridge::UdpEndpoint destination_;
    uint64_t sent_ = 0, errors_ = 0;
};
}
