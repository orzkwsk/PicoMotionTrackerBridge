#pragma once

#include <cstdint>

namespace pico_ot::internal {

// Process-local, opt-in startup observations. No pose-loop logging or public C ABI changes.
void EnableStartupDiagnostics(bool enabled);
void TraceStartupEvent(const char* event, int64_t value = 0);

}  // namespace pico_ot::internal
