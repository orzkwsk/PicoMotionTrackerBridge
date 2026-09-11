#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/protocol.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/udp_socket.hpp"

#include <array>
#include <cassert>
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
        assert(status != UdpReceiveStatus::Error);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

void TestPoseAndDevicePackets() {
    UdpSocket pc;
    std::string error;
    assert(pc.Open(0, true, &error));

    Publisher publisher;
    PublisherConfig config;
    config.pcEndpoint = {"127.0.0.1", pc.BoundPort()};
    config.senderSessionId = 0xabcddcba12344321ull;
    assert(publisher.Start(config, &error));

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

    assert(publisher.SendPose(tracker, &error));

    DecodedPacket decoded;
    UdpEndpoint source;
    assert(ReceivePacket(pc, decoded, source));
    assert(decoded.senderSessionId == config.senderSessionId);
    assert(std::holds_alternative<PosePacket>(decoded.payload));
    const auto& pose = std::get<PosePacket>(decoded.payload);
    assert(pose.serial == tracker.serial);
    assert(pose.positionValid);
    assert(pose.positionMeters.z == 3.0);
    assert(pose.rawTrackingState65 == 2);

    pico_ot::DeviceInfo device;
    device.serial = tracker.serial;
    device.runtimeDeviceId = 4;
    device.connected = true;
    device.charging = true;
    device.batteryBucketRaw = 9;
    device.observedMonotonicNs = 999;

    assert(publisher.SendDeviceState(device, &error));
    assert(ReceivePacket(pc, decoded, source));
    assert(std::holds_alternative<DeviceStatePacket>(decoded.payload));
    const auto& state = std::get<DeviceStatePacket>(decoded.payload);
    assert(state.serial == device.serial);
    assert(state.batteryBucketPresent);
    assert(state.batteryBucketRaw == 9);
}

void TestClockSyncResponse() {
    UdpSocket pc;
    std::string error;
    assert(pc.Open(0, true, &error));

    Publisher publisher;
    PublisherConfig config;
    config.pcEndpoint = {"127.0.0.1", pc.BoundPort()};
    config.senderSessionId = 0x1111222233334444ull;
    assert(publisher.Start(config, &error));

    TimeSyncRequestPacket request;
    request.nonce = 77;
    request.clientSendMonotonicNs = MonotonicNowNs();

    std::vector<uint8_t> bytes;
    assert(EncodeTimeSyncRequestPacket(0x9999, 1, request, bytes));
    assert(pc.SendTo({"127.0.0.1", publisher.LocalPort()},
                     bytes.data(), bytes.size(), &error));

    bool gotResponse = false;
    DecodedPacket decoded;
    UdpEndpoint source;
    for (int i = 0; i < 200 && !gotResponse; ++i) {
        assert(publisher.PumpControl(&error));
        if (ReceivePacket(pc, decoded, source)) {
            gotResponse = std::holds_alternative<TimeSyncResponsePacket>(decoded.payload);
            if (gotResponse) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    assert(gotResponse);
    assert(decoded.senderSessionId == config.senderSessionId);
    const auto& response = std::get<TimeSyncResponsePacket>(decoded.payload);
    assert(response.nonce == 77);
    assert(response.clientSendMonotonicNs == request.clientSendMonotonicNs);
    assert(response.serverReceiveMonotonicNs > 0);
    assert(response.serverSendMonotonicNs >= response.serverReceiveMonotonicNs);
}

}  // namespace

int main() {
    TestPoseAndDevicePackets();
    TestClockSyncResponse();
    return 0;
}
