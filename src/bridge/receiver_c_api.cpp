#include "pico_ot_bridge/receiver_c_api.h"

#include "pico_ot_bridge/receiver.hpp"

#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <vector>

struct pico_ot_bridge_receiver {
    pico_ot::bridge::Receiver receiver;
    std::string lastError;
};

namespace {

void CopySerial(const std::string& serial, char* out, size_t capacity) {
    if (capacity == 0) {
        return;
    }
    const size_t count = serial.size() < capacity - 1 ? serial.size() : capacity - 1;
    if (count > 0) {
        std::memcpy(out, serial.data(), count);
    }
    out[count] = '\0';
}

pico_ot_bridge_status_code_t SetError(
    pico_ot_bridge_receiver_t* receiver,
    pico_ot_bridge_status_code_t status,
    const std::string& message) {
    if (receiver != nullptr) {
        receiver->lastError = message;
    }
    return status;
}

void ClearError(pico_ot_bridge_receiver_t* receiver) {
    if (receiver != nullptr) {
        receiver->lastError.clear();
    }
}

}  // namespace

extern "C" {

void pico_ot_bridge_receiver_default_config(
    pico_ot_bridge_receiver_config_t* config) {
    if (config == nullptr) {
        return;
    }
    config->listen_port = 29765;
    config->time_sync_interval_ns = 1'000'000'000LL;
    config->stale_timeout_ns = 500'000'000LL;
    config->client_session_id = 0;
}

pico_ot_bridge_receiver_t* pico_ot_bridge_receiver_create(void) {
    return new (std::nothrow) pico_ot_bridge_receiver_t();
}

void pico_ot_bridge_receiver_destroy(pico_ot_bridge_receiver_t* receiver) {
    delete receiver;
}

pico_ot_bridge_status_code_t pico_ot_bridge_receiver_start(
    pico_ot_bridge_receiver_t* receiver,
    const pico_ot_bridge_receiver_config_t* config) {
    if (receiver == nullptr || config == nullptr) {
        return SetError(receiver, PICO_OT_BRIDGE_INVALID_ARGUMENT, "receiver and config are required");
    }

    pico_ot::bridge::ReceiverConfig nativeConfig;
    nativeConfig.listenPort = config->listen_port;
    nativeConfig.timeSyncIntervalNs = config->time_sync_interval_ns;
    nativeConfig.staleTimeoutNs = config->stale_timeout_ns;
    nativeConfig.clientSessionId = config->client_session_id;

    std::string error;
    if (!receiver->receiver.Start(nativeConfig, &error)) {
        return SetError(receiver, PICO_OT_BRIDGE_RECEIVER_ERROR, error);
    }
    ClearError(receiver);
    return PICO_OT_BRIDGE_OK;
}

void pico_ot_bridge_receiver_stop(pico_ot_bridge_receiver_t* receiver) {
    if (receiver == nullptr) {
        return;
    }
    receiver->receiver.Stop();
    ClearError(receiver);
}

pico_ot_bridge_status_code_t pico_ot_bridge_receiver_pump(
    pico_ot_bridge_receiver_t* receiver) {
    if (receiver == nullptr) {
        return PICO_OT_BRIDGE_INVALID_ARGUMENT;
    }
    if (!receiver->receiver.IsRunning()) {
        return SetError(receiver, PICO_OT_BRIDGE_NOT_RUNNING, "receiver is not running");
    }

    std::string error;
    if (!receiver->receiver.Pump(&error)) {
        return SetError(receiver, PICO_OT_BRIDGE_RECEIVER_ERROR, error);
    }
    ClearError(receiver);
    return PICO_OT_BRIDGE_OK;
}

uint8_t pico_ot_bridge_receiver_is_running(
    const pico_ot_bridge_receiver_t* receiver) {
    return receiver != nullptr && receiver->receiver.IsRunning() ? 1u : 0u;
}

uint16_t pico_ot_bridge_receiver_listen_port(
    const pico_ot_bridge_receiver_t* receiver) {
    return receiver != nullptr ? receiver->receiver.ListenPort() : 0u;
}

uint64_t pico_ot_bridge_receiver_active_sender_session_id(
    const pico_ot_bridge_receiver_t* receiver) {
    return receiver != nullptr ? receiver->receiver.ActiveSenderSessionId() : 0u;
}

uint32_t pico_ot_bridge_receiver_tracker_count(
    pico_ot_bridge_receiver_t* receiver) {
    if (receiver == nullptr) {
        return 0u;
    }
    const size_t count = receiver->receiver.Serials().size();
    return count > std::numeric_limits<uint32_t>::max()
               ? std::numeric_limits<uint32_t>::max()
               : static_cast<uint32_t>(count);
}

pico_ot_bridge_status_code_t pico_ot_bridge_receiver_get_tracker(
    pico_ot_bridge_receiver_t* receiver,
    uint32_t index,
    pico_ot_bridge_tracker_state_t* out) {
    if (receiver == nullptr || out == nullptr) {
        return SetError(receiver, PICO_OT_BRIDGE_INVALID_ARGUMENT, "receiver and output are required");
    }
    if (!receiver->receiver.IsRunning()) {
        return SetError(receiver, PICO_OT_BRIDGE_NOT_RUNNING, "receiver is not running");
    }

    const std::vector<std::string> serials = receiver->receiver.Serials();
    if (static_cast<size_t>(index) >= serials.size()) {
        return SetError(receiver, PICO_OT_BRIDGE_OUT_OF_RANGE, "tracker index is out of range");
    }

    const std::string& serial = serials[index];
    const pico_ot::bridge::ReceivedTrackerState* state = receiver->receiver.Find(serial);
    if (state == nullptr) {
        return SetError(receiver, PICO_OT_BRIDGE_OUT_OF_RANGE, "tracker disappeared while snapshot was read");
    }

    std::memset(out, 0, sizeof(*out));
    CopySerial(serial, out->serial, sizeof(out->serial));

    out->sender_session_id = state->senderSessionId;
    out->last_pose_sequence = state->lastPoseSequence;
    out->last_device_sequence = state->lastDeviceSequence;
    out->has_pose_sequence = state->hasPoseSequence ? 1u : 0u;
    out->has_device_sequence = state->hasDeviceSequence ? 1u : 0u;
    out->last_pose_receive_pc_ns = state->lastPoseReceivePcNs;
    out->last_device_receive_pc_ns = state->lastDeviceReceivePcNs;

    if (state->posePcMonotonicNs.has_value()) {
        out->pose_pc_monotonic_ns = *state->posePcMonotonicNs;
        out->pose_pc_monotonic_valid = 1u;
    }

    if (state->hasPoseSequence) {
        const auto& pose = state->pose;
        out->runtime_device_id = pose.runtimeDeviceId;
        out->connected_snapshot = pose.connectedSnapshot ? 1u : 0u;
        out->position_valid = pose.positionValid ? 1u : 0u;
        out->orientation_sample_present = pose.orientationSamplePresent ? 1u : 0u;
        out->raw_tracking_state_65 = pose.rawTrackingState65;
        out->reference_frame = pose.referenceFrame;
        out->angular_velocity_frame = pose.angularVelocityFrame;
        out->source_timestamp = pose.sourceTimestamp;
        out->observed_monotonic_ns = pose.observedMonotonicNs;
        out->mapped_monotonic_ns = pose.mappedMonotonicNs;
        out->mapped_monotonic_valid = pose.mappedMonotonicValid ? 1u : 0u;
        out->position_m[0] = pose.positionMeters.x;
        out->position_m[1] = pose.positionMeters.y;
        out->position_m[2] = pose.positionMeters.z;
        out->orientation_xyzw[0] = pose.orientationXyzw.x;
        out->orientation_xyzw[1] = pose.orientationXyzw.y;
        out->orientation_xyzw[2] = pose.orientationXyzw.z;
        out->orientation_xyzw[3] = pose.orientationXyzw.w;
        out->linear_velocity_mps[0] = pose.linearVelocityMetersPerSec.x;
        out->linear_velocity_mps[1] = pose.linearVelocityMetersPerSec.y;
        out->linear_velocity_mps[2] = pose.linearVelocityMetersPerSec.z;
        out->angular_velocity_radps[0] = pose.angularVelocityRadPerSec.x;
        out->angular_velocity_radps[1] = pose.angularVelocityRadPerSec.y;
        out->angular_velocity_radps[2] = pose.angularVelocityRadPerSec.z;
    } else {
        out->runtime_device_id = -1;
        out->orientation_xyzw[3] = 1.0;
    }

    if (state->deviceState.has_value()) {
        const auto& device = *state->deviceState;
        out->device_state_present = 1u;
        out->charging = device.charging ? 1u : 0u;
        out->battery_bucket_present = device.batteryBucketPresent ? 1u : 0u;
        out->battery_bucket_raw = device.batteryBucketRaw;
        if (!state->hasPoseSequence) {
            out->runtime_device_id = device.runtimeDeviceId;
            out->connected_snapshot = device.connectedSnapshot ? 1u : 0u;
        }
    }

    ClearError(receiver);
    return PICO_OT_BRIDGE_OK;
}

uint8_t pico_ot_bridge_receiver_get_clock_estimate(
    const pico_ot_bridge_receiver_t* receiver,
    pico_ot_bridge_clock_estimate_t* out) {
    if (receiver == nullptr || out == nullptr) {
        return 0u;
    }
    const auto estimate = receiver->receiver.ClockEstimate();
    if (!estimate.has_value()) {
        return 0u;
    }
    out->hmd_minus_pc_ns = estimate->hmdMinusPcNs;
    out->network_round_trip_ns = estimate->networkRoundTripNs;
    return 1u;
}

const char* pico_ot_bridge_receiver_last_error(
    const pico_ot_bridge_receiver_t* receiver) {
    return receiver != nullptr ? receiver->lastError.c_str() : "receiver is null";
}

const char* pico_ot_bridge_status_string(pico_ot_bridge_status_code_t status) {
    switch (status) {
        case PICO_OT_BRIDGE_OK:
            return "ok";
        case PICO_OT_BRIDGE_INVALID_ARGUMENT:
            return "invalid_argument";
        case PICO_OT_BRIDGE_NOT_RUNNING:
            return "not_running";
        case PICO_OT_BRIDGE_OUT_OF_RANGE:
            return "out_of_range";
        case PICO_OT_BRIDGE_RECEIVER_ERROR:
            return "receiver_error";
        default:
            return "unknown";
    }
}

}  // extern "C"
