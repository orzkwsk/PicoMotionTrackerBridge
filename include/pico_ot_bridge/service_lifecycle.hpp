#pragma once

// Windows process-lifetime control only. This is not part of POTB/C1 or the
// existing 40-byte service status ABI.
#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace pc_service_lifecycle {
constexpr std::uint32_t kMagic = 0x314c4350; // PCL1
struct State {
    std::uint32_t magic = 0, version = 1;
    std::uint64_t generation = 0;
    std::uint32_t pid = 0, ownerPid = 0;
    std::uint64_t created = 0, ownerCreated = 0;
    std::uint32_t ready = 0, reserved = 0;
};
static_assert(sizeof(State) == 48, "lifecycle state layout");

inline std::uint64_t Creation(HANDLE process) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(process, &created, &exited, &kernel, &user))
        throw std::runtime_error("process creation time unavailable");
    return (std::uint64_t(created.dwHighDateTime) << 32) | created.dwLowDateTime;
}
inline std::wstring StopName(const std::wstring& name, const State& s) {
    return name + L"_Stop_" + std::to_wstring(s.pid) + L"_" + std::to_wstring(s.created);
}
class Handle {
public:
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE v) : value(v) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { if (value) CloseHandle(value); }
};
inline HANDLE OpenIdentity(DWORD pid, std::uint64_t created) {
    HANDLE p = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) throw std::runtime_error("process identity unavailable");
    try {
        if (Creation(p) != created) throw std::runtime_error("process identity changed");
    } catch (...) { CloseHandle(p); throw; }
    return p;
}
inline void ParseOwner(const std::string& input, DWORD& pid, std::uint64_t& created) {
    const auto colon = input.find(':');
    if (colon == std::string::npos || input.find(':',colon+1) != std::string::npos || input.find_first_not_of("0123456789:") != std::string::npos)
        throw std::invalid_argument("tray owner must be PID:creation-filetime");
    auto raw = std::stoull(input.substr(0, colon));
    created = std::stoull(input.substr(colon + 1));
    if (!raw || raw > 0xffffffffULL || !created) throw std::invalid_argument("invalid tray owner identity");
    pid = static_cast<DWORD>(raw);
}

class Endpoint {
    Handle owner_, stop_, mapping_;
    State* view_ = nullptr;
public:
    Endpoint(const std::wstring& name, DWORD ownerPid = 0, std::uint64_t ownerCreated = 0) {
        if (ownerPid) owner_.value = OpenIdentity(ownerPid, ownerCreated);
        State s;
        s.pid = GetCurrentProcessId(); s.created = Creation(GetCurrentProcess());
        s.ownerPid = ownerPid; s.ownerCreated = ownerCreated;
        stop_.value = CreateEventW(nullptr, TRUE, FALSE, StopName(name, s).c_str());
        if (!stop_.value) throw std::runtime_error("lifecycle stop event unavailable");
        mapping_.value = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                           0, sizeof(State), name.c_str());
        if (!mapping_.value) throw std::runtime_error("lifecycle mapping unavailable");
        view_ = static_cast<State*>(MapViewOfFile(mapping_.value, FILE_MAP_WRITE, 0, 0, sizeof(State)));
        if (!view_) throw std::runtime_error("lifecycle mapping view unavailable");
        s.generation = 1; s.magic = kMagic;
        std::memcpy(view_, &s, sizeof(s)); MemoryBarrier(); view_->generation = 2;
    }
    Endpoint(const Endpoint&) = delete;
    ~Endpoint() { if (view_) UnmapViewOfFile(view_); }
    void MarkReady() { view_->generation = 3; MemoryBarrier(); view_->ready = 1; MemoryBarrier(); view_->generation = 4; }
    bool StopRequested() const {
        return WaitForSingleObject(stop_.value, 0) == WAIT_OBJECT_0 ||
               (owner_.value && WaitForSingleObject(owner_.value, 0) == WAIT_OBJECT_0);
    }
};

inline int StopAndWait(const std::wstring& name, DWORD timeoutMs) {
    Handle mapping(OpenFileMappingW(FILE_MAP_READ, FALSE, name.c_str()));
    if (!mapping.value) return 5; // legacy/unknown instance; never fall back to kill.
    auto view = static_cast<const State*>(MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, sizeof(State)));
    if (!view) return 5;
    State s; const auto before = view->generation;
    MemoryBarrier(); std::memcpy(&s, view, sizeof(s)); MemoryBarrier();
    const auto after = view->generation; UnmapViewOfFile(view);
    if (!before || (before & 1) || before != after || s.magic != kMagic || s.version != 1 || !s.pid) return 5;
    try {
        Handle process(OpenIdentity(s.pid, s.created));
        Handle stop(OpenEventW(EVENT_MODIFY_STATE, FALSE, StopName(name, s).c_str()));
        if (!stop.value || !SetEvent(stop.value)) return 5;
        return WaitForSingleObject(process.value, timeoutMs) == WAIT_OBJECT_0 ? 0 : 124;
    } catch (...) { return 5; }
}
} // namespace pc_service_lifecycle
