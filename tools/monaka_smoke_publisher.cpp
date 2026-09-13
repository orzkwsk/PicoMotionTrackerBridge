#include "pico_ot_backend/types.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/publisher.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: monaka_smoke_publisher <receiver-port>\n";
        return 2;
    }

    const long parsed = std::strtol(argv[1], nullptr, 10);
    if (parsed <= 0 || parsed > 65535) {
        std::cerr << "Invalid receiver port\n";
        return 2;
    }

    pico_ot::bridge::Publisher publisher;
    pico_ot::bridge::PublisherConfig config;
    config.pcEndpoint = {"127.0.0.1", static_cast<uint16_t>(parsed)};
    config.senderSessionId = 0x4d4f4e414b410001ULL;

    std::string error;
    if (!publisher.Start(config, &error)) {
        std::cerr << "Publisher start failed: " << error << "\n";
        return 1;
    }

    pico_ot::TrackerState pose;
    pose.serial = "MONAKA-SMOKE-001";
    pose.runtimeDeviceId = 42;
    pose.connectedSnapshot = true;
    pose.positionValid = true;
    pose.orientationSamplePresent = true;
    pose.sourceTimestamp = 0x12345678ULL;
    pose.observedMonotonicNs = pico_ot::bridge::MonotonicNowNs();
    pose.mappedMonotonicNs = pose.observedMonotonicNs;
    pose.mappedMonotonicValid = true;
    pose.positionMeters = {1.25, 2.5, -3.75};
    // +90 degrees around native PICO OutputA X. This intentionally remains non-identity
    // so Monaka's production composition smoke test exercises the OutputA -> room mapper.
    pose.orientationXyzw = {0.7071067811865476, 0.0, 0.0, 0.7071067811865476};
    pose.linearVelocityMetersPerSec = {0.1, 0.2, 0.3};
    pose.angularVelocityRadPerSec = {0.4, 0.5, 0.6};
    pose.frame = pico_ot::ReferenceFrame::PicoOutputA;
    pose.angularVelocityFrame = pico_ot::AngularVelocityFrame::DeviceLocalCandidate;

    if (!publisher.SendPose(pose, &error)) {
        std::cerr << "SendPose failed: " << error << "\n";
        return 1;
    }

    pico_ot::DeviceInfo device;
    device.serial = pose.serial;
    device.runtimeDeviceId = pose.runtimeDeviceId;
    device.connected = true;
    device.charging = false;
    device.batteryBucketRaw = static_cast<uint8_t>(2);
    device.observedMonotonicNs = pico_ot::bridge::MonotonicNowNs();

    if (!publisher.SendDeviceState(device, &error)) {
        std::cerr << "SendDeviceState failed: " << error << "\n";
        return 1;
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(750);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!publisher.PumpControl(&error)) {
            std::cerr << "PumpControl failed: " << error << "\n";
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    std::cout << "MONAKA_SMOKE_SENT serial=" << pose.serial
              << " session=" << config.senderSessionId << "\n";
    return 0;
}
