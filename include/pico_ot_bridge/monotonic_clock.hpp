#pragma once

#include <cstdint>

namespace pico_ot::bridge {

// Returns nanoseconds in the local process monotonic clock domain.
//
// Android/POSIX uses CLOCK_MONOTONIC to match the validated Backend's
// observed/mapped monotonic timestamps. Windows uses QueryPerformanceCounter.
[[nodiscard]] int64_t MonotonicNowNs() noexcept;

}  // namespace pico_ot::bridge
