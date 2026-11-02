#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "pico_ot_bridge/monotonic_clock.hpp"
#ifndef PICO_HMD_SOURCE
#define PICO_HMD_SOURCE "../BackendTestApp/app/src/main/cpp/backend_test_jni.cpp"
#endif
#include PICO_HMD_SOURCE

// Only the physical backend and Android platform APIs are replaced. Start/Stop,
// mutexes, poll thread, publisher, socket and snapshot are production code.
struct pico_ot_backend { size_t count; };
namespace {
int creations = 0, destructions = 0, starts = 0, stops = 0;
size_t nextCount = 0;
bool failStart = false;
void Check(bool ok, const char* what) { if (!ok) throw std::runtime_error(what); }
void StopAll() {
    { std::lock_guard<std::mutex> lock(gLifecycleMutex); StopLocked(); }
    StopPublisher();
}
}
extern "C" {
void pico_ot_default_config(pico_ot_config_t* c) { *c = {}; }
pico_ot_backend_t* pico_ot_create() { ++creations; return new pico_ot_backend{nextCount}; }
void pico_ot_destroy(pico_ot_backend_t* p) { ++destructions; delete p; }
pico_ot_status_code_t pico_ot_start(pico_ot_backend_t*, const pico_ot_config_t* c) {
    ++starts;
    Check(!c->allow_unverified_runtime && !c->allow_repeated_leaky_enumeration, "ABI/enumeration guards retained");
    Check(std::strcmp(c->expected_library_build_id_hex, kExpectedBuildId) == 0, "build ID pinned");
    return failStart ? PICO_OT_RUNTIME_NOT_READY : PICO_OT_OK;
}
void pico_ot_stop(pico_ot_backend_t*) { ++stops; }
size_t pico_ot_device_count(pico_ot_backend_t* p) { return p->count; }
pico_ot_status_code_t pico_ot_get_device(pico_ot_backend_t*, size_t i, pico_ot_device_info_t* d) {
    *d = {}; std::strcpy(d->serial, i == 0 ? "synthetic-a" : "synthetic-b");
    d->connected = 1; return PICO_OT_OK;
}
pico_ot_status_code_t pico_ot_poll_pose(pico_ot_backend_t*, const char* serial, pico_ot_pose_t* p) {
    *p = {}; std::strcpy(p->serial, serial); p->position_valid = 1; p->orientation_sample_present = 1;
    p->orientation_xyzw[3] = 1; p->observed_monotonic_ns = pico_ot::bridge::MonotonicNowNs();
    p->source_timestamp = static_cast<uint64_t>(p->observed_monotonic_ns); return PICO_OT_OK;
}
pico_ot_status_code_t pico_ot_get_runtime_diagnostics(pico_ot_backend_t*, pico_ot_runtime_diagnostics_t* d) {
    *d = {}; std::strcpy(d->library_build_id_hex, kExpectedBuildId);
    d->runtime_identity_verified = 1; return PICO_OT_OK;
}
pico_ot_status_code_t pico_ot_reinitialize_runtime(pico_ot_backend_t*) { return PICO_OT_OK; }
const char* pico_ot_last_error(pico_ot_backend_t*) { return "synthetic failure"; }
int32_t pico_ot_last_vendor_code(pico_ot_backend_t*) { return 0; }
}
int main() {
    try {
        Check(StartBackend().success, "first start");
        Check(gBackend && gBackend->count == 0, "zero devices is started, not tracking");
        auto* initial = gBackend;
        Check(StartBackend().success, "duplicate start returns success");
        Check(creations == 1 && starts == 1 && stops == 0 && gBackend == initial,
              "duplicate Start must preserve backend");
        std::cout << "PASS zero-device and idempotent Start\n";
        pico_ot::bridge::UdpSocket sink;
        std::string error;
        Check(sink.Open(0, true, &error, true, true), "ephemeral loopback sink");
        Check(StartPublisher("127.0.0.1", sink.BoundPort()).success, "publisher start");
        auto session = gPublisher.SenderSessionId(); auto port = gPublisher.LocalPort();
        Check(StartPublisher("127.0.0.1", sink.BoundPort()).success, "duplicate publisher start");
        Check(gPublisher.SenderSessionId() == session && gPublisher.LocalPort() == port,
              "same endpoint preserves session and socket");
        std::cout << "PASS idempotent publisher Start\n";
#ifndef PICO_HMD_RED_ONLY
        nextCount = 2;
        Check(StartBackend(true).success, "explicit restart");
        Check(creations == 2 && starts == 2 && stops == 1 && destructions == 1 && gBackend->count == 2,
              "restart exactly once and reenumerates");
        Check(gPublisher.SenderSessionId() == session, "backend restart does not replace independent sender");
        std::cout << "PASS explicit restart and independent publisher\n";
        for (int i = 0; i < 200 && gPoseSends.load() == 0; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        Check(gPollSuccess.load() > 0 && gPoseSends.load() > 0 && gLastPollNs.load() > 0 && gLastSendNs.load() > 0,
              "real poll loop updates send observations");
        JNIEnv env;
        const std::string snap = Java_com_orz_picootbackendtest_MainActivity_nativeSnapshot(&env, nullptr);
        Check(snap.find("Backend: RUNNING") != std::string::npos && snap.find("PC receipt: UNKNOWN") != std::string::npos,
              "native snapshot reflects runtime without claiming receipt");
        std::cout << "PASS poll/send/snapshot observation\n";
        { std::lock_guard<std::mutex> lock(gLifecycleMutex); StopLocked(); }
        Check(gPublisher.IsRunning(), "Stop Backend leaves independent publisher running");
        Check(!gRun.load() && !gPollThread.joinable() && gBackend == nullptr, "stop joins thread");
        const auto stablePolls = gPollSuccess.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        Check(gPollSuccess.load() == stablePolls, "no post-stop polls");
        std::cout << "PASS stopped poll owner\n";
        failStart = true;
        Check(!StartBackend().success && gBackend == nullptr && !gRun.load(), "failed start has no dangling owner");
        failStart = false;
        std::vector<std::thread> callers;
        const int before = creations;
        for (int i = 0; i < 8; ++i) callers.emplace_back([] { Check(StartBackend().success, "concurrent start"); });
        for (auto& t : callers) t.join();
        Check(creations == before + 1, "eight concurrent starts create one backend");
        std::cout << "PASS failure recovery and concurrent Start\n";
        Check(!StartPublisher("localhost", sink.BoundPort()).success && !gPublisher.IsRunning(), "invalid endpoint fails closed");
        Check(StartPublisher("127.0.0.1", sink.BoundPort()).success, "publisher recovery");
        StopAll(); StopAll();
        Check(creations == destructions && !gPollThread.joinable() && !gPublisher.IsRunning(),
              "explicit repeated stop releases all owned resources");
        std::cout << "PASS endpoint negative and final shutdown\n";
#endif
        StopAll(); return 0;
    } catch (const std::exception& e) {
        StopAll(); std::cerr << "FAIL " << e.what() << '\n'; return 1;
    }
}
