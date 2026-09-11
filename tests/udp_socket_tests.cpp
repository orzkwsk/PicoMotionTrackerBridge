#include "pico_ot_bridge/udp_socket.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace pico_ot::bridge;

int main() {
    UdpSocket receiver;
    UdpSocket sender;
    std::string error;

    assert(receiver.Open(0, true, &error));
    assert(receiver.BoundPort() != 0);
    assert(sender.Open(0, true, &error));

    const std::array<uint8_t, 5> payload{{1, 2, 3, 4, 5}};
    assert(sender.SendTo({"127.0.0.1", receiver.BoundPort()},
                         payload.data(), payload.size(), &error));

    std::array<uint8_t, 64> buffer{};
    size_t receivedSize = 0;
    UdpEndpoint source;

    UdpReceiveStatus status = UdpReceiveStatus::WouldBlock;
    for (int i = 0; i < 200 && status == UdpReceiveStatus::WouldBlock; ++i) {
        status = receiver.ReceiveFrom(buffer.data(), buffer.size(), receivedSize, source, &error);
        if (status == UdpReceiveStatus::WouldBlock) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    assert(status == UdpReceiveStatus::Packet);
    assert(receivedSize == payload.size());
    assert(source.address == "127.0.0.1");
    assert(source.port == sender.BoundPort());
    for (size_t i = 0; i < payload.size(); ++i) {
        assert(buffer[i] == payload[i]);
    }

    receiver.Close();
    sender.Close();
    assert(!receiver.IsOpen());
    assert(!sender.IsOpen());
    return 0;
}
