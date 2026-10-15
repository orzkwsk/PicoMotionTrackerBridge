#include "pico_ot_bridge/udp_socket.hpp"

#include <array>
#include "require.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace pico_ot::bridge;

int main() {
    // Validate before WSAStartup/socket creation as the Publisher START path does.
    for (const char* address : {"127.0.0.1", "192.168.1.10", "10.0.0.2", "255.255.255.255", "0.0.0.0"}) {
        REQUIRE(IsNumericIpv4(address));
    }
    const std::array<std::string, 15> invalid{{
        "", "localhost", "example.local", "999.1.1.1", "192.168.1",
        "192.168.1.1:29765", "::1", "999.999.999.999", " 127.0.0.1", "127.0.0.1 ",
        "+1.2.3.4", "192..1.1", "1.2.3.4.5", "192.168.001.1", std::string("127.0.0.1\0suffix", 16)
    }};
    for (const auto& address : invalid) {
        REQUIRE(!IsNumericIpv4(address));
    }

    UdpSocket receiver;
    UdpSocket sender;
    std::string error;

    REQUIRE(receiver.Open(0, true, &error));
    REQUIRE(receiver.BoundPort() != 0);
    REQUIRE(sender.Open(0, true, &error));

    const std::array<uint8_t, 5> payload{{1, 2, 3, 4, 5}};
    for (const auto& address : invalid) {
        error.clear();
        REQUIRE(!sender.SendTo({address, receiver.BoundPort()}, payload.data(), payload.size(), &error));
        REQUIRE(error == "UDP endpoint must be a numeric IPv4 address");
    }
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
