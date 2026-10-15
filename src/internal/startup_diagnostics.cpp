#include "internal/startup_diagnostics.hpp"

#include <atomic>
#include <chrono>
#include <cinttypes>
#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace pico_ot::internal {
namespace {
std::atomic<bool> gEnabled{false};
}

void EnableStartupDiagnostics(bool enabled) {
    gEnabled.store(enabled);
}

void TraceStartupEvent(const char* event, int64_t value) {
    if (!gEnabled.load()) return;
#ifdef __ANDROID__
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    const int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
    __android_log_print(ANDROID_LOG_INFO, "PicoOTStartup",
        "mono_ns=%" PRId64 " event=%s value=%" PRId64, ns, event, value);
#else
    (void)event;
    (void)value;
#endif
}

}  // namespace pico_ot::internal
