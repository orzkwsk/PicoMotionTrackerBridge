#include "pico_ot_bridge/udp_socket.hpp"

#include <array>
#include "require.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace pico_ot::bridge;

int main() {
    UdpSocket receiver;
    UdpSocket sender;
    std::string error;

    REQUIRE(receiver.Open(0, true, &error));
    REQUIRE(receiver.BoundPort() != 0);
    REQUIRE(sender.Open(0, true, &error));

    const std::array<uint8_t, 5> payload{{1, 2, 3, 4, 5}};
    REQUIRE(sender.SendTo({"127.0.0.1", receiver.BoundPort()},
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

    REQUIRE(status == UdpReceiveStatus::Packet);
    REQUIRE(receivedSize == payload.size());
    REQUIRE(source.address == "127.0.0.1");
    REQUIRE(source.port == sender.BoundPort());
    for (size_t i = 0; i < payload.size(); ++i) {
        REQUIRE(buffer[i] == payload[i]);
    }

    receiver.Close();
    sender.Close();
    REQUIRE(!receiver.IsOpen());
    REQUIRE(!sender.IsOpen());
    return 0;
}
