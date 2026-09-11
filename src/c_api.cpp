#include "pico_ot_backend/pico_ot_backend.h"

#include "pico_ot_backend/backend.hpp"

#include <algorithm>
#include <cstring>
#include <string>

using pico_ot::Backend;
using pico_ot::BackendConfig;
using pico_ot::Status;
using pico_ot::StatusCode;

struct pico_ot_backend {
    Backend backend;
    Status lastStatus{};
    std::string lastError;
};

namespace {

template <size_t N>
void CopyString(char (&dst)[N], const std::string& src) {
    std::memset(dst, 0, N);
    if constexpr (N > 0) {
        const size_t n = std::min(N - 1, src.size());
        std::memcpy(dst, src.data(), n);
    }
}

pico_ot_status_code_t ToC(StatusCode code) {
    return static_cast<pico_ot_status_code_t>(static_cast<int32_t>(code));
}

pico_ot_status_code_t Save(pico_ot_backend* h, Status status) {
    if (!h) return PICO_OT_INVALID_ARGUMENT;
    h->lastStatus = std::move(status);
    h->lastError = h->lastStatus.message;
    return ToC(h->lastStatus.code);
}

void FillDevice(const pico_ot::DeviceInfo& in, pico_ot_device_info_t* out) {
    std::memset(out, 0, sizeof(*out));
    CopyString(out->serial, in.serial);
    out->runtime_device_id = in.runtimeDeviceId;
    out->connected = in.connected ? 1 : 0;
    out->charging = in.charging ? 1 : 0;
    out->battery_bucket_present = in.batteryBucketRaw.has_value() ? 1 : 0;
    out->battery_bucket_raw = in.batteryBucketRaw.value_or(0);
    out->observed_monotonic_ns = in.observedMonotonicNs;
}

void FillPose(const pico_ot::TrackerState& in, pico_ot_pose_t* out) {
    std::memset(out, 0, sizeof(*out));
    CopyString(out->serial, in.serial);
    out->runtime_device_id = in.runtimeDeviceId;
    out->connected_snapshot = in.connectedSnapshot ? 1 : 0;
    out->position_valid = in.positionValid ? 1 : 0;
    out->orientation_sample_present = in.orientationSamplePresent ? 1 : 0;
    out->raw_tracking_state_65 = in.rawTrackingState65;
    out->source_timestamp = in.sourceTimestamp;
    out->observed_monotonic_ns = in.observedMonotonicNs;
    out->mapped_monotonic_ns = in.mappedMonotonicNs;
    out->mapped_monotonic_valid = in.mappedMonotonicValid ? 1 : 0;
    out->position_m[0] = in.positionMeters.x;
    out->position_m[1] = in.positionMeters.y;
    out->position_m[2] = in.positionMeters.z;
    out->orientation_xyzw[0] = in.orientationXyzw.x;
    out->orientation_xyzw[1] = in.orientationXyzw.y;
    out->orientation_xyzw[2] = in.orientationXyzw.z;
    out->orientation_xyzw[3] = in.orientationXyzw.w;
    out->linear_velocity_mps[0] = in.linearVelocityMetersPerSec.x;
    out->linear_velocity_mps[1] = in.linearVelocityMetersPerSec.y;
    out->linear_velocity_mps[2] = in.linearVelocityMetersPerSec.z;
    if (in.linearAccelerationMetersPerSec2) {
        out->linear_acceleration_present = 1;
        out->linear_acceleration_mps2[0] = in.linearAccelerationMetersPerSec2->x;
        out->linear_acceleration_mps2[1] = in.linearAccelerationMetersPerSec2->y;
        out->linear_acceleration_mps2[2] = in.linearAccelerationMetersPerSec2->z;
    }
    out->angular_velocity_radps[0] = in.angularVelocityRadPerSec.x;
    out->angular_velocity_radps[1] = in.angularVelocityRadPerSec.y;
    out->angular_velocity_radps[2] = in.angularVelocityRadPerSec.z;
    out->vendor_return_code = in.vendorReturnCode;
}

}  // namespace

extern "C" {

void pico_ot_default_config(pico_ot_config_t* config) {
    if (!config) return;
    std::memset(config, 0, sizeof(*config));
    config->get_body_tracker_data_offset = 0x47dc8;
    config->algo_result_size = 392;
    config->allow_unverified_runtime = 0;
    config->allow_repeated_leaky_enumeration = 0;
}

pico_ot_backend_t* pico_ot_create(void) {
    try { return new pico_ot_backend(); }
    catch (...) { return nullptr; }
}

void pico_ot_destroy(pico_ot_backend_t* backend) { delete backend; }

pico_ot_status_code_t pico_ot_start(pico_ot_backend_t* backend, const pico_ot_config_t* config) {
    if (!backend || !config) return PICO_OT_INVALID_ARGUMENT;
    BackendConfig cpp{};
    cpp.runtime.getBodyTrackerDataOffset = config->get_body_tracker_data_offset;
    cpp.runtime.algoResultSize = config->algo_result_size;
    cpp.runtime.allowUnverifiedRuntime = config->allow_unverified_runtime != 0;
    cpp.runtime.allowRepeatedLeakyEnumeration = config->allow_repeated_leaky_enumeration != 0;
    if (config->expected_library_build_id_hex) cpp.runtime.expectedLibraryBuildIdHex = config->expected_library_build_id_hex;
    return Save(backend, backend->backend.Start(cpp));
}

void pico_ot_stop(pico_ot_backend_t* backend) {
    if (!backend) return;
    backend->backend.Stop();
    backend->lastStatus = {};
    backend->lastError.clear();
}

pico_ot_status_code_t pico_ot_refresh_devices(pico_ot_backend_t* backend) {
    if (!backend) return PICO_OT_INVALID_ARGUMENT;
    return Save(backend, backend->backend.RefreshDevices());
}

pico_ot_status_code_t pico_ot_reinitialize_runtime(pico_ot_backend_t* backend) {
    if (!backend) return PICO_OT_INVALID_ARGUMENT;
    return Save(backend, backend->backend.ReinitializeRuntime());
}

size_t pico_ot_device_count(pico_ot_backend_t* backend) {
    if (!backend) return 0;
    return backend->backend.Devices().size();
}

pico_ot_status_code_t pico_ot_get_device(pico_ot_backend_t* backend, size_t index, pico_ot_device_info_t* out) {
    if (!backend || !out) return PICO_OT_INVALID_ARGUMENT;
    const auto devices = backend->backend.Devices();
    if (index >= devices.size()) return Save(backend, {StatusCode::InvalidArgument, 0, "device index out of range"});
    FillDevice(devices[index], out);
    return Save(backend, Status::Ok());
}

pico_ot_status_code_t pico_ot_poll_pose(pico_ot_backend_t* backend, const char* serial, pico_ot_pose_t* out) {
    if (!backend || !serial || !out) return PICO_OT_INVALID_ARGUMENT;
    pico_ot::TrackerState state{};
    Status status = backend->backend.Poll(serial, state);
    if (status.ok()) FillPose(state, out);
    return Save(backend, std::move(status));
}

pico_ot_status_code_t pico_ot_get_runtime_diagnostics(pico_ot_backend_t* backend, pico_ot_runtime_diagnostics_t* out) {
    if (!backend || !out) return PICO_OT_INVALID_ARGUMENT;
    std::memset(out, 0, sizeof(*out));
    const auto d = backend->backend.Diagnostics();
    CopyString(out->library_build_id_hex, d.libraryBuildIdHex);
    out->library_base = d.libraryBase;
    out->get_body_tracker_data_address = d.getBodyTrackerDataAddress;
    out->get_body_tracker_data_offset = d.getBodyTrackerDataOffset;
    out->intentionally_leaked_enumeration_bytes = d.intentionallyLeakedEnumerationBytes;
    out->runtime_identity_verified = d.runtimeIdentityVerified ? 1 : 0;
    return Save(backend, Status::Ok());
}

const char* pico_ot_last_error(pico_ot_backend_t* backend) {
    return backend ? backend->lastError.c_str() : "invalid backend";
}

int32_t pico_ot_last_vendor_code(pico_ot_backend_t* backend) {
    return backend ? backend->lastStatus.vendorCode : 0;
}

}  // extern "C"
