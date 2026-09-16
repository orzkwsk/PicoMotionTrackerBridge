#include "pico_ot_bridge/protocol.hpp"

#include "require.hpp"
#include <cmath>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

using namespace pico_ot::bridge;

namespace {

constexpr uint64_t kSession = 0x0102030405060708ull;

bool Near(double a, double b) {
    return std::abs(a - b) < 1e-12;
}

void TestPoseRoundTrip() {
    PosePacket src;
    src.serial = "PC2310MLK7200684G";
    src.runtimeDeviceId = 4;
    src.connectedSnapshot = true;
    src.positionValid = true;
    src.orientationSamplePresent = true;
    src.mappedMonotonicValid = true;
    src.rawTrackingState65 = 3;
    src.sourceTimestamp = 0x1122334455667788ull;
    src.observedMonotonicNs = 1234567890123ll;
    src.mappedMonotonicNs = 1234567890999ll;
    src.positionMeters = {1.25, -2.5, 3.75};
    src.orientationXyzw = {-0.1, 0.2, -0.3, 0.9};
    src.linearVelocityMetersPerSec = {0.4, -0.5, 0.6};
    src.angularVelocityRadPerSec = {-1.1, 1.2, -1.3};
    src.referenceFrame = 0;
    src.angularVelocityFrame = 1;

    std::vector<uint8_t> bytes;
    REQUIRE(EncodePosePacket(kSession, 42, src, bytes));
    REQUIRE(bytes.size() == kWireHeaderSize + 174);

    DecodedPacket decoded;
    REQUIRE(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    REQUIRE(decoded.senderSessionId == kSession);
    REQUIRE(decoded.sequence == 42);
    REQUIRE(std::holds_alternative<PosePacket>(decoded.payload));

    const auto& out = std::get<PosePacket>(decoded.payload);
    REQUIRE(out.serial == src.serial);
    REQUIRE(out.runtimeDeviceId == 4);
    REQUIRE(out.connectedSnapshot);
    REQUIRE(out.positionValid);
    REQUIRE(out.orientationSamplePresent);
    REQUIRE(out.mappedMonotonicValid);
    REQUIRE(out.rawTrackingState65 == 3);
    REQUIRE(out.sourceTimestamp == src.sourceTimestamp);
    REQUIRE(out.observedMonotonicNs == src.observedMonotonicNs);
    REQUIRE(out.mappedMonotonicNs == src.mappedMonotonicNs);
    REQUIRE(Near(out.positionMeters.x, 1.25));
    REQUIRE(Near(out.positionMeters.y, -2.5));
    REQUIRE(Near(out.positionMeters.z, 3.75));
    REQUIRE(Near(out.orientationXyzw.x, -0.1));
    REQUIRE(Near(out.orientationXyzw.w, 0.9));
    REQUIRE(Near(out.linearVelocityMetersPerSec.z, 0.6));
    REQUIRE(Near(out.angularVelocityRadPerSec.y, 1.2));
    REQUIRE(out.referenceFrame == 0);
    REQUIRE(out.angularVelocityFrame == 1);
}

void TestDeviceStateRoundTrip() {
    DeviceStatePacket src;
    src.serial = "PC2310MLK8040979G";
    src.runtimeDeviceId = 3;
    src.connectedSnapshot = true;
    src.charging = true;
    src.batteryBucketPresent = true;
    src.batteryBucketRaw = 9;
    src.observedMonotonicNs = 9988776655ll;

    std::vector<uint8_t> bytes;
    REQUIRE(EncodeDeviceStatePacket(kSession, 7, src, bytes));

    DecodedPacket decoded;
    REQUIRE(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    REQUIRE(decoded.senderSessionId == kSession);
    REQUIRE(decoded.sequence == 7);
    REQUIRE(std::holds_alternative<DeviceStatePacket>(decoded.payload));

    const auto& out = std::get<DeviceStatePacket>(decoded.payload);
    REQUIRE(out.serial == src.serial);
    REQUIRE(out.runtimeDeviceId == 3);
    REQUIRE(out.connectedSnapshot);
    REQUIRE(out.charging);
    REQUIRE(out.batteryBucketPresent);
    REQUIRE(out.batteryBucketRaw == 9);
    REQUIRE(out.observedMonotonicNs == src.observedMonotonicNs);
}

void TestTimeSyncRoundTrip() {
    TimeSyncRequestPacket req{123, 1000};
    std::vector<uint8_t> bytes;
    REQUIRE(EncodeTimeSyncRequestPacket(kSession, 8, req, bytes));

    DecodedPacket decoded;
    REQUIRE(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    REQUIRE(decoded.senderSessionId == kSession);
    const auto& reqOut = std::get<TimeSyncRequestPacket>(decoded.payload);
    REQUIRE(reqOut.nonce == 123);
    REQUIRE(reqOut.clientSendMonotonicNs == 1000);

    TimeSyncResponsePacket res{123, 1000, 1100, 1150};
    REQUIRE(EncodeTimeSyncResponsePacket(kSession, 9, res, bytes));
    REQUIRE(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    REQUIRE(decoded.senderSessionId == kSession);
    const auto& resOut = std::get<TimeSyncResponsePacket>(decoded.payload);
    REQUIRE(resOut.nonce == 123);
    REQUIRE(resOut.clientSendMonotonicNs == 1000);
    REQUIRE(resOut.serverReceiveMonotonicNs == 1100);
    REQUIRE(resOut.serverSendMonotonicNs == 1150);
}

void TestMalformedPackets() {
    PosePacket pose;
    pose.serial = "PC2310MLK7200684G";
    std::vector<uint8_t> bytes;
    REQUIRE(EncodePosePacket(kSession, 1, pose, bytes));

    DecodedPacket decoded;
    REQUIRE(DecodePacket(bytes.data(), kWireHeaderSize - 1, decoded) == DecodeStatus::Truncated);

    auto badMagic = bytes;
    badMagic[0] ^= 0xff;
    REQUIRE(DecodePacket(badMagic.data(), badMagic.size(), decoded) == DecodeStatus::BadMagic);

    auto badVersion = bytes;
    badVersion[4] = 2;
    badVersion[5] = 0;
    REQUIRE(DecodePacket(badVersion.data(), badVersion.size(), decoded) == DecodeStatus::UnsupportedVersion);

    auto truncated = bytes;
    truncated.pop_back();
    REQUIRE(DecodePacket(truncated.data(), truncated.size(), decoded) == DecodeStatus::InvalidPayloadSize);

    PosePacket nanPose = pose;
    nanPose.positionMeters.x = std::numeric_limits<double>::quiet_NaN();
    REQUIRE(!EncodePosePacket(kSession, 1, nanPose, bytes));

    PosePacket longSerial = pose;
    longSerial.serial.assign(kWireSerialCapacity, 'A');
    REQUIRE(!EncodePosePacket(kSession, 1, longSerial, bytes));
}

}  // namespace

int main() {
    TestPoseRoundTrip();
    TestDeviceStateRoundTrip();
    TestTimeSyncRoundTrip();
    TestMalformedPackets();
    return 0;
}
