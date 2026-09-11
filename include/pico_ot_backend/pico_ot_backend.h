#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PICO_OT_SERIAL_CAPACITY 32
#define PICO_OT_BUILD_ID_CAPACITY 96
#define PICO_OT_ERROR_CAPACITY 256

typedef struct pico_ot_backend pico_ot_backend_t;

typedef enum pico_ot_status_code {
    PICO_OT_OK = 0,
    PICO_OT_INVALID_ARGUMENT,
    PICO_OT_UNSUPPORTED_PLATFORM,
    PICO_OT_LIBRARY_OPEN_FAILED,
    PICO_OT_SYMBOL_MISSING,
    PICO_OT_RUNTIME_ABI_MISMATCH,
    PICO_OT_RUNTIME_BUILD_MISMATCH,
    PICO_OT_RUNTIME_NOT_READY,
    PICO_OT_VENDOR_ERROR,
    PICO_OT_TRACKER_NOT_FOUND,
    PICO_OT_ENUMERATION_REFRESH_DISABLED,
    PICO_OT_CORRUPT_VENDOR_OUTPUT,
} pico_ot_status_code_t;

typedef struct pico_ot_config {
    uintptr_t get_body_tracker_data_offset;
    size_t algo_result_size;
    const char* expected_library_build_id_hex;
    uint8_t allow_unverified_runtime;
    uint8_t allow_repeated_leaky_enumeration;
} pico_ot_config_t;

typedef struct pico_ot_device_info {
    char serial[PICO_OT_SERIAL_CAPACITY];
    int32_t runtime_device_id;
    uint8_t connected;
    uint8_t charging;
    uint8_t battery_bucket_present;
    uint8_t battery_bucket_raw;
    int64_t observed_monotonic_ns;
} pico_ot_device_info_t;

typedef struct pico_ot_pose {
    char serial[PICO_OT_SERIAL_CAPACITY];
    int32_t runtime_device_id;
    uint8_t connected_snapshot;
    uint8_t position_valid;
    uint8_t orientation_sample_present;
    uint8_t raw_tracking_state_65;

    uint64_t source_timestamp;
    int64_t observed_monotonic_ns;
    int64_t mapped_monotonic_ns;
    uint8_t mapped_monotonic_valid;

    double position_m[3];
    double orientation_xyzw[4];
    double linear_velocity_mps[3];
    double linear_acceleration_mps2[3];
    uint8_t linear_acceleration_present;
    double angular_velocity_radps[3];

    int32_t vendor_return_code;
} pico_ot_pose_t;

typedef struct pico_ot_runtime_diagnostics {
    char library_build_id_hex[PICO_OT_BUILD_ID_CAPACITY];
    uintptr_t library_base;
    uintptr_t get_body_tracker_data_address;
    uintptr_t get_body_tracker_data_offset;
    uint64_t intentionally_leaked_enumeration_bytes;
    uint8_t runtime_identity_verified;
} pico_ot_runtime_diagnostics_t;

void pico_ot_default_config(pico_ot_config_t* config);
pico_ot_backend_t* pico_ot_create(void);
void pico_ot_destroy(pico_ot_backend_t* backend);

pico_ot_status_code_t pico_ot_start(pico_ot_backend_t* backend, const pico_ot_config_t* config);
void pico_ot_stop(pico_ot_backend_t* backend);
pico_ot_status_code_t pico_ot_refresh_devices(pico_ot_backend_t* backend);
pico_ot_status_code_t pico_ot_reinitialize_runtime(pico_ot_backend_t* backend);

size_t pico_ot_device_count(pico_ot_backend_t* backend);
pico_ot_status_code_t pico_ot_get_device(pico_ot_backend_t* backend, size_t index, pico_ot_device_info_t* out);
pico_ot_status_code_t pico_ot_poll_pose(pico_ot_backend_t* backend, const char* serial, pico_ot_pose_t* out);
pico_ot_status_code_t pico_ot_get_runtime_diagnostics(pico_ot_backend_t* backend, pico_ot_runtime_diagnostics_t* out);

const char* pico_ot_last_error(pico_ot_backend_t* backend);
int32_t pico_ot_last_vendor_code(pico_ot_backend_t* backend);

#ifdef __cplusplus
}
#endif
