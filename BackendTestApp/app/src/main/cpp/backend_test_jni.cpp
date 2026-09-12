#include <jni.h>
#include <android/log.h>

#include "pico_ot_backend/pico_ot_backend.h"
#include "pico_ot_bridge/publisher.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

constexpr const char* kTag = "PicoOTTest";
constexpr const char* kExpectedBuildId = "d1b55bc0598ffff93cc01eaf089793f8";
constexpr uint32_t kPollHz = 120;
constexpr uint32_t kPoseLogHz = 10;
constexpr uint32_t kPoseLogEveryLoops = kPollHz / kPoseLogHz;
constexpr uint32_t kDeviceStateEveryLoops = kPollHz * 2;
constexpr auto kPollPeriod = std::chrono::nanoseconds(1000000000LL / kPollHz);

std::mutex gLifecycleMutex;
pico_ot_backend_t* gBackend = nullptr;
std::thread gPollThread;
std::atomic<bool> gRun{false};

std::mutex gPublisherMutex;
pico_ot::bridge::Publisher gPublisher;
bool gPublisherErrorLogged = false;

struct CadenceStats {
    uint64_t polls = 0;
    uint64_t uniqueSamples = 0;
    uint64_t repeatedSamples = 0;
    uint64_t lastSourceTimestamp = 0;
    bool hasLastSourceTimestamp = false;
    uint64_t minUniqueDeltaNs = std::numeric_limits<uint64_t>::max();
    uint64_t maxUniqueDeltaNs = 0;
    long double uniqueDeltaSumNs = 0.0L;
    uint64_t uniqueDeltaCount = 0;
};

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

void LogPublisherErrorOnce(const std::string& where, const std::string& error) {
    if (gPublisherErrorLogged) return;
    gPublisherErrorLogged = true;
    __android_log_print(
        ANDROID_LOG_ERROR, kTag,
        "BRIDGE %s failed: %s",
        where.c_str(), error.c_str());
}

pico_ot::DeviceInfo ToDeviceInfo(const pico_ot_device_info_t& dev) {
    pico_ot::DeviceInfo out;
    out.serial = dev.serial;
    out.runtimeDeviceId = dev.runtime_device_id;
    out.connected = dev.connected != 0;
    out.charging = dev.charging != 0;
    if (dev.battery_bucket_present != 0) {
        out.batteryBucketRaw = dev.battery_bucket_raw;
    }
    out.observedMonotonicNs = dev.observed_monotonic_ns;
    return out;
}

pico_ot::TrackerState ToTrackerState(const pico_ot_pose_t& pose) {
    pico_ot::TrackerState out;
    out.serial = pose.serial;
    out.runtimeDeviceId = pose.runtime_device_id;
    out.connectedSnapshot = pose.connected_snapshot != 0;
    out.positionValid = pose.position_valid != 0;
    out.orientationSamplePresent = pose.orientation_sample_present != 0;
    out.rawTrackingState65 = pose.raw_tracking_state_65;
    out.sourceTimestamp = pose.source_timestamp;
    out.observedMonotonicNs = pose.observed_monotonic_ns;
    out.mappedMonotonicNs = pose.mapped_monotonic_ns;
    out.mappedMonotonicValid = pose.mapped_monotonic_valid != 0;
    out.positionMeters = {pose.position_m[0], pose.position_m[1], pose.position_m[2]};
    out.orientationXyzw = {
        pose.orientation_xyzw[0], pose.orientation_xyzw[1],
        pose.orientation_xyzw[2], pose.orientation_xyzw[3]};
    out.linearVelocityMetersPerSec = {
        pose.linear_velocity_mps[0], pose.linear_velocity_mps[1], pose.linear_velocity_mps[2]};
    out.angularVelocityRadPerSec = {
        pose.angular_velocity_radps[0], pose.angular_velocity_radps[1], pose.angular_velocity_radps[2]};
    out.frame = pico_ot::ReferenceFrame::PicoOutputA;
    out.angularVelocityFrame = pico_ot::AngularVelocityFrame::DeviceLocalCandidate;
    return out;
}

void SendDeviceStateIfRunning(const pico_ot_device_info_t& dev) {
    std::lock_guard<std::mutex> lock(gPublisherMutex);
    if (!gPublisher.IsRunning()) return;

    std::string error;
    if (!gPublisher.SendDeviceState(ToDeviceInfo(dev), &error)) {
        LogPublisherErrorOnce("device state send", error);
    }
}

void SendPoseIfRunning(const pico_ot_pose_t& pose) {
    std::lock_guard<std::mutex> lock(gPublisherMutex);
    if (!gPublisher.IsRunning()) return;

    std::string error;
    if (!gPublisher.SendPose(ToTrackerState(pose), &error)) {
        LogPublisherErrorOnce("pose send", error);
    }
}

void PumpPublisherIfRunning() {
    std::lock_guard<std::mutex> lock(gPublisherMutex);
    if (!gPublisher.IsRunning()) return;

    std::string error;
    if (!gPublisher.PumpControl(&error)) {
        LogPublisherErrorOnce("control pump", error);
    }
}

void ResetCadenceWindow(CadenceStats& stats) {
    stats.polls = 0;
    stats.uniqueSamples = 0;
    stats.repeatedSamples = 0;
    stats.minUniqueDeltaNs = std::numeric_limits<uint64_t>::max();
    stats.maxUniqueDeltaNs = 0;
    stats.uniqueDeltaSumNs = 0.0L;
    stats.uniqueDeltaCount = 0;
}

uint64_t ObserveCadence(CadenceStats& stats, uint64_t sourceTimestamp) {
    ++stats.polls;

    uint64_t delta = 0;
    if (!stats.hasLastSourceTimestamp) {
        stats.hasLastSourceTimestamp = true;
        stats.lastSourceTimestamp = sourceTimestamp;
        ++stats.uniqueSamples;
        return 0;
    }

    if (sourceTimestamp == stats.lastSourceTimestamp) {
        ++stats.repeatedSamples;
        return 0;
    }

    ++stats.uniqueSamples;
    if (sourceTimestamp > stats.lastSourceTimestamp) {
        delta = sourceTimestamp - stats.lastSourceTimestamp;
        if (delta < stats.minUniqueDeltaNs) stats.minUniqueDeltaNs = delta;
        if (delta > stats.maxUniqueDeltaNs) stats.maxUniqueDeltaNs = delta;
        stats.uniqueDeltaSumNs += static_cast<long double>(delta);
        ++stats.uniqueDeltaCount;
    }

    stats.lastSourceTimestamp = sourceTimestamp;
    return delta;
}

void LogCadenceWindow(const std::string& serial, const CadenceStats& stats) {
    const uint64_t minDelta = stats.uniqueDeltaCount == 0 ? 0 : stats.minUniqueDeltaNs;
    const uint64_t avgDelta = stats.uniqueDeltaCount == 0
        ? 0
        : static_cast<uint64_t>(stats.uniqueDeltaSumNs / static_cast<long double>(stats.uniqueDeltaCount));
    const double estimatedSourceHz = avgDelta == 0
        ? 0.0
        : 1000000000.0 / static_cast<double>(avgDelta);

    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "CADENCE sn=%s targetPollHz=%u polls=%" PRIu64 " unique=%" PRIu64 " repeats=%" PRIu64
        " minDeltaNs=%" PRIu64 " avgDeltaNs=%" PRIu64 " maxDeltaNs=%" PRIu64 " estSourceHz=%.2f",
        serial.c_str(),
        kPollHz,
        stats.polls,
        stats.uniqueSamples,
        stats.repeatedSamples,
        minDelta,
        avgDelta,
        stats.maxUniqueDeltaNs,
        estimatedSourceHz);
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
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "POLL_CONFIG targetPollHz=%u poseLogHz=%u periodNs=%" PRId64,
        kPollHz,
        kPoseLogHz,
        static_cast<int64_t>(kPollPeriod.count()));

    std::unordered_map<std::string, CadenceStats> cadence;
    std::unordered_map<std::string, int> lastPositionValid;
    uint64_t loop = 0;

    while (gRun.load()) {
        const auto deadline = std::chrono::steady_clock::now() + kPollPeriod;
        const bool sendDeviceState = (loop % kDeviceStateEveryLoops) == 0;

        const size_t count = pico_ot_device_count(backend);
        for (size_t i = 0; i < count && gRun.load(); ++i) {
            pico_ot_device_info_t dev{};
            auto rc = pico_ot_get_device(backend, i, &dev);
            if (rc != PICO_OT_OK) {
                LogError(backend, "get_device", rc);
                continue;
            }

            if (sendDeviceState) {
                SendDeviceStateIfRunning(dev);
            }
            if (!dev.connected) continue;

            pico_ot_pose_t pose{};
            rc = pico_ot_poll_pose(backend, dev.serial, &pose);
            if (rc != PICO_OT_OK) {
                LogError(backend, "poll_pose", rc);
                continue;
            }

            SendPoseIfRunning(pose);

            const std::string serial(dev.serial);
            auto& stats = cadence[serial];
            const uint64_t dtSource = ObserveCadence(stats, pose.source_timestamp);

            const int currentPositionValid = pose.position_valid ? 1 : 0;
            const auto validIt = lastPositionValid.find(serial);
            const bool validityChanged = validIt == lastPositionValid.end() || validIt->second != currentPositionValid;
            lastPositionValid[serial] = currentPositionValid;

            const bool periodicPoseLog = (loop % kPoseLogEveryLoops) == 0;
            if (periodicPoseLog || validityChanged) {
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
        }

        PumpPublisherIfRunning();

        ++loop;
        if ((loop % kPollHz) == 0) {
            for (auto& entry : cadence) {
                LogCadenceWindow(entry.first, entry.second);
                ResetCadenceWindow(entry.second);
            }
        }

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
    cfg.expected_library_build_id_hex = kExpectedBuildId;
    cfg.allow_unverified_runtime = 0;
    cfg.allow_repeated_leaky_enumeration = 0;

    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "NATIVE StartBackend: calling pico_ot_start offset=0x%" PRIxPTR
        " algoSize=%zu allowUnverified=%u expectedBuildId=%s",
        cfg.get_body_tracker_data_offset,
        cfg.algo_result_size,
        cfg.allow_unverified_runtime,
        cfg.expected_library_build_id_hex);

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
        << "\nVerified: " << static_cast<unsigned>(diag.runtime_identity_verified)
        << "\nPoll: " << kPollHz << " Hz"
        << "\nLogcat: PicoOTTest";
    LogInfo("NATIVE StartBackend: complete");
    return out.str();
}

std::string StartPublisher(const std::string& pcIpv4, uint16_t port) {
    std::lock_guard<std::mutex> lock(gPublisherMutex);

    pico_ot::bridge::PublisherConfig config;
    config.pcEndpoint = {pcIpv4, port};
    config.localPort = 0;

    std::string error;
    if (!gPublisher.Start(config, &error)) {
        return "Bridge start failed: " + error;
    }

    gPublisherErrorLogged = false;
    __android_log_print(
        ANDROID_LOG_INFO, kTag,
        "BRIDGE started pc=%s:%u localPort=%u session=%" PRIu64,
        pcIpv4.c_str(),
        static_cast<unsigned>(port),
        static_cast<unsigned>(gPublisher.LocalPort()),
        gPublisher.SenderSessionId());

    std::ostringstream out;
    out << "UDP bridge started\nPC: " << pcIpv4 << ":" << port
        << "\nLocal port: " << gPublisher.LocalPort()
        << "\nSession: " << gPublisher.SenderSessionId();
    return out.str();
}

void StopPublisher() {
    std::lock_guard<std::mutex> lock(gPublisherMutex);
    if (gPublisher.IsRunning()) {
        LogInfo("BRIDGE stopping");
    }
    gPublisher.Stop();
    gPublisherErrorLogged = false;
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

extern "C" JNIEXPORT jstring JNICALL
Java_com_orz_picootbackendtest_MainActivity_nativeStartBridge(
    JNIEnv* env, jclass, jstring pcIpv4, jint port) {
    if (pcIpv4 == nullptr || port <= 0 || port > 65535) {
        return ToJString(env, "Bridge start failed: invalid endpoint");
    }

    const char* chars = env->GetStringUTFChars(pcIpv4, nullptr);
    if (chars == nullptr) {
        return ToJString(env, "Bridge start failed: invalid PC IPv4");
    }
    const std::string ip(chars);
    env->ReleaseStringUTFChars(pcIpv4, chars);

    return ToJString(env, StartPublisher(ip, static_cast<uint16_t>(port)));
}

extern "C" JNIEXPORT void JNICALL
Java_com_orz_picootbackendtest_MainActivity_nativeStopBridge(JNIEnv*, jclass) {
    StopPublisher();
}
