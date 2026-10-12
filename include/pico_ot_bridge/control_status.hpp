#pragma once
#include <cstddef>
#include <cstdint>
namespace pico_ot::bridge {
// Driver-independent, read-only control snapshot. The PC ingress owner writes
// at most four times/second. No pose vectors or vendor/private-ABI fields are exposed.
// uptimeMs is Windows GetTickCount64 (shared OS boot clock), ageMs is fixed
// receive age at publication. Odd sequence means a write is in progress.
constexpr wchar_t kControlStatusMapping[] = L"Local\\PicoOTBridge_ControlStatus.v1";
constexpr wchar_t kControlStopEvent[] = L"Local\\PicoOTBridge_ServiceStop.v1";
struct ControlDeviceStatus {
    char serial[32]{};
    std::uint32_t flags = 0; // connected snapshot=1, pose observed=2, full position=4, full orientation=8
    std::uint32_t reserved = 0;
    std::int64_t ageMs = -1;
};
struct ControlStatus {
    std::uint32_t magic = 0x43544F50, version = 1;
    std::int64_t sequence = 0;
    std::uint64_t uptimeMs = 0;
    std::uint32_t pid = 0, route = 0, count = 0, reserved = 0;
    ControlDeviceStatus devices[5];
};
static_assert(sizeof(ControlDeviceStatus)==48 && sizeof(ControlStatus)==280);
static_assert(offsetof(ControlStatus,devices)==40 && offsetof(ControlStatus,sequence)==8);
} // namespace pico_ot::bridge
