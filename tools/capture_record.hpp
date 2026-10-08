#pragma once
#include <cstdint>
#include <cstddef>
#include <string>

namespace pico_ot::capture {
struct HostStamp { int64_t ticks; int64_t frequency; };
// Windows: raw QPC, the same domain as .NET Stopwatch.GetTimestamp(), never
// MonotonicNowNs(). POSIX: CLOCK_MONOTONIC ns, for standalone synthetic tests.
HostStamp HostNow();
const char* HostClockKind();
std::string UtcNow();
std::string DatagramRecord(const uint8_t* data, size_t size, HostStamp stamp,
                           const std::string& utc);
}  // namespace pico_ot::capture
