#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/protocol.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/udp_socket.hpp"

#include <array>
#include "require.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using namespace pico_ot::bridge;

namespace {

bool ReceivePacket(UdpSocket& socket, DecodedPacket& decoded, UdpEndpoint& source) {
    std::array<uint8_t, 512> buffer{};
    for (int i = 0; i < 200; ++i) {
        size_t receivedSize = 0;
        std::string error;
        const auto status = socket.ReceiveFrom(
            buffer.data(), buffer.size(), receivedSize, source, &error);
        if (status == UdpReceiveStatus::Packet) {
            return DecodePacket(buffer.data(), receivedSize, decoded) == DecodeStatus::Ok;
        }
        REQUIRE(status != UdpReceiveStatus::Error);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void TestPoseAndDevicePackets() {
    UdpSocket pc;
    std::string error;
    REQUIRE(pc.Open(0, true, &error));

    Publisher publisher;
    PublisherConfig config;
    config.pcEndpoint = {"127.0.0.1", pc.BoundPort()};
    config.senderSessionId = 0xabcddcba12344321ull;
    REQUIRE(publisher.Start(config, &error));

    pico_ot::TrackerState tracker;
    tracker.serial = "PC2310MLK7200684G";
    tracker.runtimeDeviceId = 4;
    tracker.connectedSnapshot = true;
    tracker.positionValid = true;
    tracker.orientationSamplePresent = true;
    tracker.mappedMonotonicValid = true;
    tracker.sourceTimestamp = 123;
    tracker.observedMonotonicNs = 456;
    tracker.mappedMonotonicNs = 789;
    tracker.positionMeters = {1.0, 2.0, 3.0};
    tracker.orientationXyzw = {0.0, 0.0, 0.0, 1.0};
    tracker.linearVelocityMetersPerSec = {0.1, 0.2, 0.3};
    tracker.angularVelocityRadPerSec = {0.4, 0.5, 0.6};
    tracker.rawTrackingState65 = 2;
    tracker.frame = pico_ot::ReferenceFrame::PicoOutputA;
    tracker.angularVelocityFrame = pico_ot::AngularVelocityFrame::DeviceLocalCandidate;

    REQUIRE(publisher.SendPose(tracker, &error));

    DecodedPacket decoded;
    UdpEndpoint source;
    REQUIRE(ReceivePacket(pc, decoded, source));
    REQUIRE(decoded.senderSessionId == config.senderSessionId);
    REQUIRE(std::holds_alternative<PosePacket>(decoded.payload));
    const auto& pose = std::get<PosePacket>(decoded.payload);
    REQUIRE(pose.serial == tracker.serial);
    REQUIRE(pose.positionValid);
    REQUIRE(pose.positionMeters.z == 3.0);
    REQUIRE(pose.rawTrackingState65 == 2);

    pico_ot::DeviceInfo device;
    device.serial = tracker.serial;
    device.runtimeDeviceId = 4;
    device.connected = true;
    device.charging = true;
    device.batteryBucketRaw = static_cast<uint8_t>(9);
    device.observedMonotonicNs = 999;

    REQUIRE(publisher.SendDeviceState(device, &error));
    REQUIRE(ReceivePacket(pc, decoded, source));
    REQUIRE(std::holds_alternative<DeviceStatePacket>(decoded.payload));
    const auto& state = std::get<DeviceStatePacket>(decoded.payload);
    REQUIRE(state.serial == device.serial);
    REQUIRE(state.batteryBucketPresent);
    REQUIRE(state.batteryBucketRaw == 9);
}

void TestClockSyncResponse() {
    UdpSocket pc;
    std::string error;
    REQUIRE(pc.Open(0, true, &error));

    Publisher publisher;
    PublisherConfig config;
    config.pcEndpoint = {"127.0.0.1", pc.BoundPort()};
    config.senderSessionId = 0x1111222233334444ull;
    REQUIRE(publisher.Start(config, &error));

    TimeSyncRequestPacket request;
    request.nonce = 77;
    request.clientSendMonotonicNs = MonotonicNowNs();

    std::vector<uint8_t> bytes;
    REQUIRE(EncodeTimeSyncRequestPacket(0x9999, 1, request, bytes));
    REQUIRE(pc.SendTo({"127.0.0.1", publisher.LocalPort()},
                     bytes.data(), bytes.size(), &error));

    bool gotResponse = false;
    DecodedPacket decoded;
    UdpEndpoint source;
    for (int i = 0; i < 200 && !gotResponse; ++i) {
        REQUIRE(publisher.PumpControl(&error));
        if (ReceivePacket(pc, decoded, source)) {
            gotResponse = std::holds_alternative<TimeSyncResponsePacket>(decoded.payload);
            if (gotResponse) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    REQUIRE(gotResponse);
    REQUIRE(decoded.senderSessionId == config.senderSessionId);
    const auto& response = std::get<TimeSyncResponsePacket>(decoded.payload);
    REQUIRE(response.nonce == 77);
    REQUIRE(response.clientSendMonotonicNs == request.clientSendMonotonicNs);
    REQUIRE(response.serverReceiveMonotonicNs > 0);
    REQUIRE(response.serverSendMonotonicNs >= response.serverReceiveMonotonicNs);
}

void TestEndpointValidation() {
    Publisher publisher;
    PublisherConfig config;
    std::string error;
    for (const char* address : {"127.0.0.1", "192.168.1.10", "10.0.0.2", "255.255.255.255"}) {
        config.pcEndpoint = {address, 29765};
        REQUIRE(publisher.Start(config, &error));
        REQUIRE(publisher.IsRunning());
    }
    for (const char* address : {"", "localhost", "example.local", "999.1.1.1",
                                "192.168.1", "192.168.1.1:29765", "::1", "999.999.999.999"}) {
        // Starting with a bad endpoint also tears down a previously running sender.
        config.pcEndpoint = {"127.0.0.1", 29765};
        REQUIRE(publisher.Start(config, &error));
        config.pcEndpoint.address = address;
        error.clear();
        REQUIRE(!publisher.Start(config, &error));
        REQUIRE(!error.empty());
        REQUIRE(!publisher.IsRunning());
        REQUIRE(publisher.LocalPort() == 0);
        REQUIRE(publisher.SenderSessionId() == 0);
    }
    config.pcEndpoint = {"127.0.0.1", 0};
    REQUIRE(!publisher.Start(config, &error));
    REQUIRE(!publisher.IsRunning());
}

void TestRestartAndStop() {
    UdpSocket pcA;
    UdpSocket pcB;
    std::string error;
    REQUIRE(pcA.Open(0, true, &error));
    REQUIRE(pcB.Open(0, true, &error));
    Publisher publisher;
    PublisherConfig config;
    pico_ot::DeviceInfo device;
    device.serial = "restart-test";
    uint64_t lastSession = 0;
    for (int i = 0; i < 20; ++i) {
        UdpSocket& destination = (i % 2) == 0 ? pcA : pcB;
        config.pcEndpoint = {"127.0.0.1", destination.BoundPort()};
        REQUIRE(publisher.Start(config, &error));
        REQUIRE(publisher.SenderSessionId() != 0);
        REQUIRE(publisher.SenderSessionId() != lastSession);
        lastSession = publisher.SenderSessionId();
        REQUIRE(publisher.SendDeviceState(device, &error));
        DecodedPacket decoded;
        UdpEndpoint source;
        REQUIRE(ReceivePacket(destination, decoded, source));
        REQUIRE(decoded.senderSessionId == lastSession);
        REQUIRE(decoded.sequence == 0);
        REQUIRE(source.port == publisher.LocalPort());
    }
    publisher.Stop();
    publisher.Stop();
    REQUIRE(!publisher.IsRunning());
    REQUIRE(publisher.LocalPort() == 0);
    REQUIRE(publisher.SenderSessionId() == 0);
    REQUIRE(!publisher.SendDeviceState(device, &error));
    REQUIRE(!publisher.PumpControl(&error));
}

void TestSocketFailureAndRecovery() {
    UdpSocket occupied;
    std::string error;
    REQUIRE(occupied.Open(0, true, &error, true));
    Publisher publisher;
    PublisherConfig config;
    config.pcEndpoint = {"127.0.0.1", 29765};
    config.localPort = occupied.BoundPort();
    REQUIRE(!publisher.Start(config, &error));
    REQUIRE(!error.empty());
    REQUIRE(!publisher.IsRunning());
    REQUIRE(publisher.LocalPort() == 0);
    REQUIRE(publisher.SenderSessionId() == 0);
    config.localPort = 0;
    REQUIRE(publisher.Start(config, &error));
    REQUIRE(publisher.IsRunning());
}

}  // namespace

int main() {
    TestPoseAndDevicePackets();
    TestClockSyncResponse();
    TestEndpointValidation();
    TestRestartAndStop();
    TestSocketFailureAndRecovery();
    return 0;
}
