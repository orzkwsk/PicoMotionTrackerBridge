#include <jni.h>
#include <android/log.h>

#include "pico_ot_backend/pico_ot_backend.h"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

constexpr const char* kTag = "PicoOTTest";
constexpr auto kPollPeriod = std::chrono::milliseconds(100); // 10 Hz initial validation

std::mutex gLifecycleMutex;
pico_ot_backend_t* gBackend = nullptr;
std::thread gPollThread;
std::atomic<bool> gRun{false};

void LogInfo(const char* message) {
    __android_log_print(ANDROID_LOG_INFO, kTag, "%s", message);
}

void LogError(pico_ot_backend_t* backend, const char* where, pico_ot_status_code_t rc) {
    __android_log_print(
        ANDROID_LOG_ERROR, kTag,
        "%s rc=%d vendor=%d error=%s",
        where,
        static_cast<int>(rc),
        backend ? pico_ot_last_vendor_code(backend) : 0,
        backend ? pico_ot_last_error(backend) : "no backend");
}

// Caller owns gLifecycleMutex. PollLoop never takes this mutex; gBackend stays alive
// until the polling thread has observed gRun=false and has been joined.
void StopLocked() {
    LogInfo("NATIVE StopLocked: begin");
    gRun.store(false);
    if (gPollThread.joinable()) {
        LogInfo("NATIVE StopLocked: joining poll thread");
        gPollThread.join();
        LogInfo("NATIVE StopLocked: poll thread joined");
    }
    if (gBackend != nullptr) {
        LogInfo("NATIVE StopLocked: stopping backend");
        pico_ot_stop(gBackend);
        pico_ot_destroy(gBackend);
        gBackend = nullptr;
    }
    LogInfo("NATIVE StopLocked: complete");
}

void PollLoop(pico_ot_backend_t* backend) {
    LogInfo("NATIVE PollLoop: started");
    std::unordered_map<std::string, uint64_t> lastSourceTimestamp;
    uint64_t loop = 0;

    while (gRun.load()) {
        const auto deadline = std::chrono::steady_clock::now() + kPollPeriod;

        const size_t count = pico_ot_device_count(backend);
        for (size_t i = 0; i < count && gRun.load(); ++i) {
            pico_ot_device_info_t dev{};
            auto rc = pico_ot_get_device(backend, i, &dev);
            if (rc != PICO_OT_OK) {
                LogError(backend, "get_device", rc);
                continue;
            }
            if (!dev.connected) continue;

            pico_ot_pose_t pose{};
            rc = pico_ot_poll_pose(backend, dev.serial, &pose);
            if (rc != PICO_OT_OK) {
                LogError(backend, "poll_pose", rc);
                continue;
            }

            const std::string serial(dev.serial);
            const auto it = lastSourceTimestamp.find(serial);
            const uint64_t dtSource = (it == lastSourceTimestamp.end() || pose.source_timestamp < it->second)
                ? 0 : pose.source_timestamp - it->second;
            lastSourceTimestamp[serial] = pose.source_timestamp;

            __android_log_print(
                ANDROID_LOG_INFO, kTag,
                "POSE loop=%" PRIu64 " sn=%s id=%d conn=%u posValid=%u ori=%u state65=%u "
                "src=%" PRIu64 " dSrc=%" PRIu64 " obs=%" PRId64 " mapped=%" PRId64 " mappedValid=%u "
                "p=(%.5f,%.5f,%.5f) q=(%.7f,%.7f,%.7f,%.7f) "
                "v=(%.5f,%.5f,%.5f) w=(%.5f,%.5f,%.5f)",
                loop,
                pose.serial,
                pose.runtime_device_id,
                pose.connected_snapshot,
                pose.position_valid,
                pose.orientation_sample_present,
                pose.raw_tracking_state_65,
                pose.source_timestamp,
                dtSource,
                pose.observed_monotonic_ns,
                pose.mapped_monotonic_ns,
                pose.mapped_monotonic_valid,
                pose.position_m[0], pose.position_m[1], pose.position_m[2],
                pose.orientation_xyzw[0], pose.orientation_xyzw[1], pose.orientation_xyzw[2], pose.orientation_xyzw[3],
                pose.linear_velocity_mps[0], pose.linear_velocity_mps[1], pose.linear_velocity_mps[2],
                pose.angular_velocity_radps[0], pose.angular_velocity_radps[1], pose.angular_velocity_radps[2]);
        }

        ++loop;
        std::this_thread::sleep_until(deadline);
    }
    LogInfo("NATIVE PollLoop: exiting");
}

std::string StartBackend() {
    LogInfo("NATIVE StartBackend: enter");
    std::lock_guard<std::mutex> lock(gLifecycleMutex);
    LogInfo("NATIVE StartBackend: lifecycle lock acquired");
    StopLocked();

    LogInfo("NATIVE StartBackend: pico_ot_create");
    gBackend = pico_ot_create();
    if (gBackend == nullptr) {
        __android_log_print(ANDROID_LOG_ERROR, kTag, "NATIVE StartBackend: pico_ot_create failed");
        return "pico_ot_create failed";
    }
    LogInfo("NATIVE StartBackend: pico_ot_create ok");

    pico_ot_config_t cfg{};
    pico_ot_default_config(&cfg);
    // Initial device validation only. After obtaining the actual GNU Build ID,
    // pin expected_library_build_id_hex and turn this override off.
    cfg.allow_unverified_runtime = 1;
    cfg.allow_repeated_leaky_enumeration = 0;

    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "NATIVE StartBackend: calling pico_ot_start offset=0x%" PRIxPTR " algoSize=%zu allowUnverified=%u",
        cfg.get_body_tracker_data_offset,
        cfg.algo_result_size,
        cfg.allow_unverified_runtime);

    const auto rc = pico_ot_start(gBackend, &cfg);
    __android_log_print(ANDROID_LOG_INFO, kTag, "NATIVE StartBackend: pico_ot_start returned rc=%d", static_cast<int>(rc));
    if (rc != PICO_OT_OK) {
        LogError(gBackend, "start", rc);
        const std::string error = pico_ot_last_error(gBackend);
        pico_ot_destroy(gBackend);
        gBackend = nullptr;
        return "Start failed: " + error;
    }

    pico_ot_runtime_diagnostics_t diag{};
    const auto diagRc = pico_ot_get_runtime_diagnostics(gBackend, &diag);
    __android_log_print(ANDROID_LOG_INFO, kTag, "NATIVE StartBackend: diagnostics rc=%d", static_cast<int>(diagRc));
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "RUNTIME buildId=%s verified=%u base=0x%" PRIxPTR " body=0x%" PRIxPTR
        " offset=0x%" PRIxPTR " leakedEnumBytes=%" PRIu64,
        diag.library_build_id_hex,
        diag.runtime_identity_verified,
        diag.library_base,
        diag.get_body_tracker_data_address,
        diag.get_body_tracker_data_offset,
        diag.intentionally_leaked_enumeration_bytes);

    const size_t count = pico_ot_device_count(gBackend);
    __android_log_print(ANDROID_LOG_INFO, kTag, "NATIVE StartBackend: device_count=%zu", count);
    for (size_t i = 0; i < count; ++i) {
        pico_ot_device_info_t dev{};
        if (pico_ot_get_device(gBackend, i, &dev) != PICO_OT_OK) continue;
        __android_log_print(
            ANDROID_LOG_INFO, kTag,
            "DEVICE index=%zu sn=%s id=%d connected=%u charging=%u batteryPresent=%u batteryRaw=%u observed=%" PRId64,
            i,
            dev.serial,
            dev.runtime_device_id,
            dev.connected,
            dev.charging,
            dev.battery_bucket_present,
            dev.battery_bucket_raw,
            dev.observed_monotonic_ns);
    }

    gRun.store(true);
    pico_ot_backend_t* pollingBackend = gBackend;
    gPollThread = std::thread([pollingBackend] { PollLoop(pollingBackend); });

    std::ostringstream out;
    out << "Backend started\nTrackers: " << count
        << "\nBuild ID: " << diag.library_build_id_hex
        << "\nLogcat: PicoOTTest";
    LogInfo("NATIVE StartBackend: complete");
    return out.str();
}

jstring ToJString(JNIEnv* env, const std::string& text) {
    return env->NewStringUTF(text.c_str());
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_orz_picootbackendtest_MainActivity_nativeStart(JNIEnv* env, jclass) {
    LogInfo("JNI nativeStart: enter");
    const std::string result = StartBackend();
    LogInfo("JNI nativeStart: return");
    return ToJString(env, result);
}

extern "C" JNIEXPORT void JNICALL
Java_com_orz_picootbackendtest_MainActivity_nativeStop(JNIEnv*, jclass) {
    LogInfo("JNI nativeStop: enter");
    std::lock_guard<std::mutex> lock(gLifecycleMutex);
    StopLocked();
    LogInfo("JNI nativeStop: return");
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_orz_picootbackendtest_MainActivity_nativeReinitialize(JNIEnv* env, jclass) {
    LogInfo("JNI nativeReinitialize: enter");
    std::lock_guard<std::mutex> lock(gLifecycleMutex);
    if (gBackend == nullptr) return ToJString(env, "Backend is not running");
    const auto rc = pico_ot_reinitialize_runtime(gBackend);
    if (rc != PICO_OT_OK) {
        LogError(gBackend, "reinitialize", rc);
        return ToJString(env, std::string("Reinitialize failed: ") + pico_ot_last_error(gBackend));
    }
    __android_log_print(ANDROID_LOG_INFO, kTag, "REINITIALIZE status=ok trackerMap=rebuilt clockMapper=reset");
    return ToJString(env, "Tracker map reinitialized; clock mapping reset");
}
