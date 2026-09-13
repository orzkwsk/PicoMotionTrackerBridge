#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#  ifdef PICO_OT_BRIDGE_BUILD_DLL
#    define PICO_OT_BRIDGE_API __declspec(dllexport)
#  else
#    define PICO_OT_BRIDGE_API __declspec(dllimport)
#  endif
#else
#  define PICO_OT_BRIDGE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define PICO_OT_BRIDGE_SERIAL_CAPACITY 32
#define PICO_OT_BRIDGE_ERROR_CAPACITY 256

typedef struct pico_ot_bridge_receiver pico_ot_bridge_receiver_t;

typedef enum pico_ot_bridge_status_code {
    PICO_OT_BRIDGE_OK = 0,
    PICO_OT_BRIDGE_INVALID_ARGUMENT,
    PICO_OT_BRIDGE_NOT_RUNNING,
    PICO_OT_BRIDGE_OUT_OF_RANGE,
    PICO_OT_BRIDGE_RECEIVER_ERROR,
} pico_ot_bridge_status_code_t;

typedef struct pico_ot_bridge_receiver_config {
    uint16_t listen_port;
    int64_t time_sync_interval_ns;
    int64_t stale_timeout_ns;
    uint64_t client_session_id;
} pico_ot_bridge_receiver_config_t;

typedef struct pico_ot_bridge_tracker_state {
    char serial[PICO_OT_BRIDGE_SERIAL_CAPACITY];

    uint64_t sender_session_id;
    uint32_t last_pose_sequence;
    uint32_t last_device_sequence;
    uint8_t has_pose_sequence;
    uint8_t has_device_sequence;

    int32_t runtime_device_id;
    uint8_t connected_snapshot;
    uint8_t position_valid;
    uint8_t orientation_sample_present;
    uint8_t raw_tracking_state_65;
    uint8_t reference_frame;
    uint8_t angular_velocity_frame;

    int64_t last_pose_receive_pc_ns;
    int64_t last_device_receive_pc_ns;
    int64_t pose_pc_monotonic_ns;
    uint8_t pose_pc_monotonic_valid;

    uint64_t source_timestamp;
    int64_t observed_monotonic_ns;
    int64_t mapped_monotonic_ns;
    uint8_t mapped_monotonic_valid;

    double position_m[3];
    double orientation_xyzw[4];
    double linear_velocity_mps[3];
    double angular_velocity_radps[3];

    uint8_t device_state_present;
    uint8_t charging;
    uint8_t battery_bucket_present;
    uint8_t battery_bucket_raw;
} pico_ot_bridge_tracker_state_t;

typedef struct pico_ot_bridge_clock_estimate {
    int64_t hmd_minus_pc_ns;
    int64_t network_round_trip_ns;
} pico_ot_bridge_clock_estimate_t;

PICO_OT_BRIDGE_API void pico_ot_bridge_receiver_default_config(
    pico_ot_bridge_receiver_config_t* config);

PICO_OT_BRIDGE_API pico_ot_bridge_receiver_t* pico_ot_bridge_receiver_create(void);
PICO_OT_BRIDGE_API void pico_ot_bridge_receiver_destroy(
    pico_ot_bridge_receiver_t* receiver);

PICO_OT_BRIDGE_API pico_ot_bridge_status_code_t pico_ot_bridge_receiver_start(
    pico_ot_bridge_receiver_t* receiver,
    const pico_ot_bridge_receiver_config_t* config);
PICO_OT_BRIDGE_API void pico_ot_bridge_receiver_stop(
    pico_ot_bridge_receiver_t* receiver);
PICO_OT_BRIDGE_API pico_ot_bridge_status_code_t pico_ot_bridge_receiver_pump(
    pico_ot_bridge_receiver_t* receiver);

PICO_OT_BRIDGE_API uint8_t pico_ot_bridge_receiver_is_running(
    const pico_ot_bridge_receiver_t* receiver);
PICO_OT_BRIDGE_API uint16_t pico_ot_bridge_receiver_listen_port(
    const pico_ot_bridge_receiver_t* receiver);
PICO_OT_BRIDGE_API uint64_t pico_ot_bridge_receiver_active_sender_session_id(
    const pico_ot_bridge_receiver_t* receiver);

PICO_OT_BRIDGE_API size_t pico_ot_bridge_receiver_tracker_count(
    pico_ot_bridge_receiver_t* receiver);
PICO_OT_BRIDGE_API pico_ot_bridge_status_code_t pico_ot_bridge_receiver_get_tracker(
    pico_ot_bridge_receiver_t* receiver,
    size_t index,
    pico_ot_bridge_tracker_state_t* out);

PICO_OT_BRIDGE_API uint8_t pico_ot_bridge_receiver_get_clock_estimate(
    const pico_ot_bridge_receiver_t* receiver,
    pico_ot_bridge_clock_estimate_t* out);

PICO_OT_BRIDGE_API const char* pico_ot_bridge_receiver_last_error(
    const pico_ot_bridge_receiver_t* receiver);
PICO_OT_BRIDGE_API const char* pico_ot_bridge_status_string(
    pico_ot_bridge_status_code_t status);

#ifdef __cplusplus
}
#endif
