#include "pico_ot_bridge/protocol.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <variant>
#include <vector>

using namespace pico_ot::bridge;

namespace {

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
    assert(EncodePosePacket(42, src, bytes));
    assert(bytes.size() == kWireHeaderSize + 174);

    DecodedPacket decoded;
    assert(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    assert(decoded.sequence == 42);
    assert(std::holds_alternative<PosePacket>(decoded.payload));

    const auto& out = std::get<PosePacket>(decoded.payload);
    assert(out.serial == src.serial);
    assert(out.runtimeDeviceId == 4);
    assert(out.connectedSnapshot);
    assert(out.positionValid);
    assert(out.orientationSamplePresent);
    assert(out.mappedMonotonicValid);
    assert(out.rawTrackingState65 == 3);
    assert(out.sourceTimestamp == src.sourceTimestamp);
    assert(out.observedMonotonicNs == src.observedMonotonicNs);
    assert(out.mappedMonotonicNs == src.mappedMonotonicNs);
    assert(Near(out.positionMeters.x, 1.25));
    assert(Near(out.positionMeters.y, -2.5));
    assert(Near(out.positionMeters.z, 3.75));
    assert(Near(out.orientationXyzw.x, -0.1));
    assert(Near(out.orientationXyzw.w, 0.9));
    assert(Near(out.linearVelocityMetersPerSec.z, 0.6));
    assert(Near(out.angularVelocityRadPerSec.y, 1.2));
    assert(out.referenceFrame == 0);
    assert(out.angularVelocityFrame == 1);
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
    assert(EncodeDeviceStatePacket(7, src, bytes));

    DecodedPacket decoded;
    assert(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    assert(decoded.sequence == 7);
    assert(std::holds_alternative<DeviceStatePacket>(decoded.payload));

    const auto& out = std::get<DeviceStatePacket>(decoded.payload);
    assert(out.serial == src.serial);
    assert(out.runtimeDeviceId == 3);
    assert(out.connectedSnapshot);
    assert(out.charging);
    assert(out.batteryBucketPresent);
    assert(out.batteryBucketRaw == 9);
    assert(out.observedMonotonicNs == src.observedMonotonicNs);
}

void TestTimeSyncRoundTrip() {
    TimeSyncRequestPacket req{123, 1000};
    std::vector<uint8_t> bytes;
    assert(EncodeTimeSyncRequestPacket(8, req, bytes));

    DecodedPacket decoded;
    assert(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    const auto& reqOut = std::get<TimeSyncRequestPacket>(decoded.payload);
    assert(reqOut.nonce == 123);
    assert(reqOut.clientSendMonotonicNs == 1000);

    TimeSyncResponsePacket res{123, 1000, 1100, 1150};
    assert(EncodeTimeSyncResponsePacket(9, res, bytes));
    assert(DecodePacket(bytes.data(), bytes.size(), decoded) == DecodeStatus::Ok);
    const auto& resOut = std::get<TimeSyncResponsePacket>(decoded.payload);
    assert(resOut.nonce == 123);
    assert(resOut.clientSendMonotonicNs == 1000);
    assert(resOut.serverReceiveMonotonicNs == 1100);
    assert(resOut.serverSendMonotonicNs == 1150);
}

void TestMalformedPackets() {
    PosePacket pose;
    pose.serial = "PC2310MLK7200684G";
    std::vector<uint8_t> bytes;
    assert(EncodePosePacket(1, pose, bytes));

    DecodedPacket decoded;
    assert(DecodePacket(bytes.data(), kWireHeaderSize - 1, decoded) == DecodeStatus::Truncated);

    auto badMagic = bytes;
    badMagic[0] ^= 0xff;
    assert(DecodePacket(badMagic.data(), badMagic.size(), decoded) == DecodeStatus::BadMagic);

    auto badVersion = bytes;
    badVersion[4] = 2;
    badVersion[5] = 0;
    assert(DecodePacket(badVersion.data(), badVersion.size(), decoded) == DecodeStatus::UnsupportedVersion);

    auto truncated = bytes;
    truncated.pop_back();
    assert(DecodePacket(truncated.data(), truncated.size(), decoded) == DecodeStatus::InvalidPayloadSize);

    PosePacket nanPose = pose;
    nanPose.positionMeters.x = std::numeric_limits<double>::quiet_NaN();
    assert(!EncodePosePacket(1, nanPose, bytes));

    PosePacket longSerial = pose;
    longSerial.serial.assign(kWireSerialCapacity, 'A');
    assert(!EncodePosePacket(1, longSerial, bytes));
}

}  // namespace

int main() {
    TestPoseRoundTrip();
    TestDeviceStateRoundTrip();
    TestTimeSyncRoundTrip();
    TestMalformedPackets();
    return 0;
}
