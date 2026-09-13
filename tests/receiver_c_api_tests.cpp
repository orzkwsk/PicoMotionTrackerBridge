#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/receiver_c_api.h"
#include "pico_ot_bridge/service_contract.hpp"

#include <cassert>
#include <chrono>
#include <cstring>
#include <string>
#include <thread>

namespace {

pico_ot::TrackerState MakePose() {
    pico_ot::TrackerState state;
    state.serial = "PICO-CAPI-001";
    state.runtimeDeviceId = 7;
    state.connectedSnapshot = true;
    state.positionValid = true;
    state.orientationSamplePresent = true;
    state.mappedMonotonicValid = true;
    state.sourceTimestamp = 1234;
    state.observedMonotonicNs = pico_ot::bridge::MonotonicNowNs();
    state.mappedMonotonicNs = state.observedMonotonicNs;
    state.positionMeters = {1.0, 2.0, 3.0};
    state.orientationXyzw = {0.0, 0.0, 0.0, 1.0};
    state.linearVelocityMetersPerSec = {0.1, 0.2, 0.3};
    state.angularVelocityRadPerSec = {0.4, 0.5, 0.6};
    state.frame = pico_ot::ReferenceFrame::PicoOutputA;
    state.angularVelocityFrame = pico_ot::AngularVelocityFrame::DeviceLocalCandidate;
    return state;
}

void PumpPair(
    pico_ot_bridge_receiver_t* receiver,
    pico_ot::bridge::Publisher& publisher,
    int iterations = 10) {
    std::string error;
    for (int i = 0; i < iterations; ++i) {
        assert(pico_ot_bridge_receiver_pump(receiver) == PICO_OT_BRIDGE_OK);
        assert(publisher.PumpControl(&error));
        assert(pico_ot_bridge_receiver_pump(receiver) == PICO_OT_BRIDGE_OK);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void TestAbiMetadata() {
    assert(pico_ot_bridge_c_api_version() == PICO_OT_BRIDGE_C_API_VERSION);
    assert(pico_ot_bridge_receiver_config_size() == sizeof(pico_ot_bridge_receiver_config_t));
    assert(pico_ot_bridge_tracker_state_size() == sizeof(pico_ot_bridge_tracker_state_t));
    assert(pico_ot_bridge_clock_estimate_size() == sizeof(pico_ot_bridge_clock_estimate_t));
}

void TestPoseAndDeviceStateRoundTrip() {
    pico_ot_bridge_receiver_t* receiver = pico_ot_bridge_receiver_create();
    assert(receiver != nullptr);

    pico_ot_bridge_receiver_config_t config{};
    pico_ot_bridge_receiver_default_config(&config);
    config.listen_port = 0;
    config.time_sync_interval_ns = 1'000'000LL;
    config.client_session_id = 0x1234;
    assert(pico_ot_bridge_receiver_start(receiver, &config) == PICO_OT_BRIDGE_OK);
    assert(pico_ot_bridge_receiver_is_running(receiver) == 1);
    assert(pico_ot_bridge_receiver_listen_port(receiver) != 0);

    pico_ot::bridge::Publisher publisher;
    pico_ot::bridge::PublisherConfig publisherConfig;
    publisherConfig.pcEndpoint = {"127.0.0.1", pico_ot_bridge_receiver_listen_port(receiver)};
    publisherConfig.senderSessionId = 0x7777;
    std::string error;
    assert(publisher.Start(publisherConfig, &error));

    const auto pose = MakePose();
    assert(publisher.SendPose(pose, &error));
    PumpPair(receiver, publisher, 5);

    assert(pico_ot_bridge_receiver_active_sender_session_id(receiver) == 0x7777);
    assert(pico_ot_bridge_receiver_tracker_count(receiver) == 1);

    pico_ot_bridge_tracker_state_t tracker{};
    assert(pico_ot_bridge_receiver_get_tracker(receiver, 0, &tracker) == PICO_OT_BRIDGE_OK);
    assert(std::strcmp(tracker.serial, "PICO-CAPI-001") == 0);
    assert(tracker.sender_session_id == 0x7777);
    assert(tracker.has_pose_sequence == 1);
    assert(tracker.runtime_device_id == 7);
    assert(tracker.connected_snapshot == 1);
    assert(tracker.position_valid == 1);
    assert(tracker.orientation_sample_present == 1);
    assert(tracker.last_pose_receive_pc_ns > 0);
    assert(tracker.position_m[0] == 1.0);
    assert(tracker.position_m[1] == 2.0);
    assert(tracker.position_m[2] == 3.0);
    assert(tracker.orientation_xyzw[3] == 1.0);
    assert(tracker.reference_frame == static_cast<uint8_t>(pico_ot::ReferenceFrame::PicoOutputA));

    pico_ot::DeviceInfo device;
    device.serial = pose.serial;
    device.runtimeDeviceId = pose.runtimeDeviceId;
    device.connected = true;
    device.charging = true;
    device.batteryBucketRaw = static_cast<uint8_t>(3);
    device.observedMonotonicNs = pico_ot::bridge::MonotonicNowNs();
    assert(publisher.SendDeviceState(device, &error));
    PumpPair(receiver, publisher, 3);

    assert(pico_ot_bridge_receiver_get_tracker(receiver, 0, &tracker) == PICO_OT_BRIDGE_OK);
    assert(tracker.has_device_sequence == 1);
    assert(tracker.device_state_present == 1);
    assert(tracker.charging == 1);
    assert(tracker.battery_bucket_present == 1);
    assert(tracker.battery_bucket_raw == 3);

    PumpPair(receiver, publisher, 10);
    pico_ot_bridge_clock_estimate_t clock{};
    assert(pico_ot_bridge_receiver_get_clock_estimate(receiver, &clock) == 1);

    assert(pico_ot_bridge_receiver_get_tracker(receiver, 1, &tracker) == PICO_OT_BRIDGE_OUT_OF_RANGE);
    assert(std::strlen(pico_ot_bridge_receiver_last_error(receiver)) > 0);

    pico_ot_bridge_receiver_stop(receiver);
    assert(pico_ot_bridge_receiver_is_running(receiver) == 0);
    assert(pico_ot_bridge_receiver_pump(receiver) == PICO_OT_BRIDGE_NOT_RUNNING);
    pico_ot_bridge_receiver_destroy(receiver);
}

void TestInvalidArguments() {
    pico_ot_bridge_receiver_config_t config{};
    pico_ot_bridge_receiver_default_config(&config);
    assert(config.listen_port == pico_ot::bridge::kMonakaFanoutPort);
    assert(config.time_sync_interval_ns > 0);
    assert(config.stale_timeout_ns > 0);

    assert(pico_ot_bridge_receiver_start(nullptr, &config) == PICO_OT_BRIDGE_INVALID_ARGUMENT);
    assert(pico_ot_bridge_receiver_pump(nullptr) == PICO_OT_BRIDGE_INVALID_ARGUMENT);
    assert(pico_ot_bridge_receiver_get_tracker(nullptr, 0, nullptr) == PICO_OT_BRIDGE_INVALID_ARGUMENT);
    assert(std::strcmp(pico_ot_bridge_status_string(PICO_OT_BRIDGE_OK), "ok") == 0);
}

}  // namespace

int main() {
    TestAbiMetadata();
    TestPoseAndDeviceStateRoundTrip();
    TestInvalidArguments();
    return 0;
}
