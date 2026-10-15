#include "runtime/current_firmware_adapter.hpp"

#include "internal/decoder.hpp"
#include "internal/startup_diagnostics.hpp"

#include <dlfcn.h>
#include <elf.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace pico_ot {
namespace {

constexpr uintptr_t kScratch0 = static_cast<uintptr_t>(0xA5A5A5A5A5A5A5A5ULL);
constexpr uintptr_t kScratch1 = static_cast<uintptr_t>(0x5A5A5A5A5A5A5A5AULL);
constexpr size_t kTrackerInfoSize = 72;
constexpr size_t kMaxTrackerInfos = 5;
constexpr uint8_t kCanaryByte = 0xA5;

constexpr const char* kGetInstanceSymbol = "_ZN3PXR14TrackingClient11getInstanceEv";
constexpr const char* kInitTrackerMapSymbol = "_ZN3PXR14TrackingClient14initTrackerMapEv";
constexpr const char* kGetConnectStateSymbol =
    "_ZN3PXR14TrackingClient27getSwiftTrackerConnectStateERNSt3__16vectorINS_8Tracking11TrackerInfoENS1_9allocatorIS4_EEEE";

extern "C" uintptr_t pico_ot_call_noarg_with_sret_scratch(void* function, void* scratch);

struct RawVector3Ptr {
    uint8_t* begin = nullptr;
    uint8_t* end = nullptr;
    uint8_t* cap = nullptr;
};

struct alignas(16) RawAlgoResultTracker {
    uint8_t bytes[internal::kOutputBufferSize];
};

using GetBodyTrackerDataFn = int32_t (*)(
    void*, const std::string&, int64_t, RawAlgoResultTracker&, RawAlgoResultTracker&);
using InitTrackerMapFn = void (*)(void*);
using GetConnectStateFn = int32_t (*)(void*, RawVector3Ptr*);

int64_t MonotonicNowNs() {
    timespec ts{};
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

void* OpenTrackingClient() {
    dlerror();
    void* handle = dlopen("libtrackingclient.pxr.so", RTLD_NOW | RTLD_LOCAL);
    if (handle != nullptr) return handle;
    dlerror();
    return dlopen("/system_ext/lib64/libtrackingclient.pxr.so", RTLD_NOW | RTLD_LOCAL);
}

void* Resolve(void* handle, const char* symbol) {
    if (handle == nullptr) return nullptr;
    dlerror();
    return dlsym(handle, symbol);
}

bool AddressIsExecutable(const void* address) {
    const uintptr_t needle = reinterpret_cast<uintptr_t>(address);
    std::ifstream maps("/proc/self/maps");
    std::string line;
    while (std::getline(maps, line)) {
        unsigned long long start = 0;
        unsigned long long end = 0;
        char perms[5] = {};
        if (std::sscanf(line.c_str(), "%llx-%llx %4s", &start, &end, perms) == 3 &&
            needle >= start && needle < end) {
            return std::strchr(perms, 'x') != nullptr;
        }
    }
    return false;
}

int32_t ReadI32(const uint8_t* data, size_t offset) {
    int32_t value = 0;
    std::memcpy(&value, data + offset, sizeof(value));
    return value;
}

std::string ReadSerial(const uint8_t* data) {
    std::string serial;
    serial.reserve(18);
    for (size_t i = 18; i < 36; ++i) {
        const uint8_t c = data[i];
        if (c == 0) break;
        if (c < 32 || c > 126) return {};
        serial.push_back(static_cast<char>(c));
    }
    return serial;
}

size_t Align4(size_t value) { return (value + 3U) & ~size_t(3U); }

std::string BytesToHex(const uint8_t* data, size_t size) {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(size * 2);
    for (size_t i = 0; i < size; ++i) {
        out.push_back(kHex[(data[i] >> 4) & 0x0f]);
        out.push_back(kHex[data[i] & 0x0f]);
    }
    return out;
}

std::string ReadGnuBuildId(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};

    Elf64_Ehdr eh{};
    in.read(reinterpret_cast<char*>(&eh), sizeof(eh));
    if (!in || std::memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 ||
        eh.e_ident[EI_CLASS] != ELFCLASS64 || eh.e_phentsize != sizeof(Elf64_Phdr)) {
        return {};
    }

    in.seekg(static_cast<std::streamoff>(eh.e_phoff), std::ios::beg);
    std::vector<Elf64_Phdr> phdrs(eh.e_phnum);
    in.read(reinterpret_cast<char*>(phdrs.data()), static_cast<std::streamsize>(phdrs.size() * sizeof(Elf64_Phdr)));
    if (!in) return {};

    for (const auto& ph : phdrs) {
        if (ph.p_type != PT_NOTE || ph.p_filesz == 0 || ph.p_filesz > (1U << 20)) continue;
        std::vector<uint8_t> notes(static_cast<size_t>(ph.p_filesz));
        in.clear();
        in.seekg(static_cast<std::streamoff>(ph.p_offset), std::ios::beg);
        in.read(reinterpret_cast<char*>(notes.data()), static_cast<std::streamsize>(notes.size()));
        if (!in) continue;

        size_t off = 0;
        while (off + sizeof(Elf64_Nhdr) <= notes.size()) {
            Elf64_Nhdr nh{};
            std::memcpy(&nh, notes.data() + off, sizeof(nh));
            off += sizeof(nh);
            const size_t nameOff = off;
            const size_t descOff = off + Align4(nh.n_namesz);
            const size_t next = descOff + Align4(nh.n_descsz);
            if (next > notes.size()) break;

            const bool isGnu = nh.n_namesz >= 3 &&
                std::memcmp(notes.data() + nameOff, "GNU", 3) == 0;
            if (isGnu && nh.n_type == NT_GNU_BUILD_ID && nh.n_descsz > 0) {
                return BytesToHex(notes.data() + descOff, nh.n_descsz);
            }
            off = next;
        }
    }
    return {};
}

std::string ToLower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

}  // namespace

struct CurrentFirmwareRuntimeAdapter::Impl {
    RuntimeConfig config{};
    RuntimeDiagnostics diagnostics{};
    void* library = nullptr;
    void* instance = nullptr;
    InitTrackerMapFn initTrackerMap = nullptr;
    GetConnectStateFn getConnectState = nullptr;
    GetBodyTrackerDataFn getBodyTrackerData = nullptr;
    bool started = false;
    bool enumerationUsed = false;
};

CurrentFirmwareRuntimeAdapter::CurrentFirmwareRuntimeAdapter() : impl_(new Impl()) {}
CurrentFirmwareRuntimeAdapter::~CurrentFirmwareRuntimeAdapter() { Stop(); delete impl_; }

Status CurrentFirmwareRuntimeAdapter::Start(const RuntimeConfig& config) {
    internal::TraceStartupEvent("runtime.start.enter");
#if !defined(__ANDROID__) || !defined(__aarch64__)
    (void)config;
    return {StatusCode::UnsupportedPlatform, 0, "current PICO runtime adapter requires Android arm64"};
#else
    Stop();
    impl_->config = config;

    if (sizeof(std::string) != 24) {
        return {StatusCode::RuntimeAbiMismatch, 0, "vendor ABI requires sizeof(std::string)==24"};
    }
    if (config.algoResultSize != internal::kObservedAlgoResultSize) {
        return {StatusCode::RuntimeAbiMismatch, 0, "only the observed 392-byte algo_result_tracker layout is implemented"};
    }

    impl_->library = OpenTrackingClient();
    internal::TraceStartupEvent("runtime.dlopen.return", impl_->library != nullptr ? 1 : 0);
    if (impl_->library == nullptr) {
        const char* e = dlerror();
        return {StatusCode::LibraryOpenFailed, 0, e ? e : "dlopen(libtrackingclient.pxr.so) failed"};
    }

    void* getInstance = Resolve(impl_->library, kGetInstanceSymbol);
    void* init = Resolve(impl_->library, kInitTrackerMapSymbol);
    void* enumerate = Resolve(impl_->library, kGetConnectStateSymbol);
    if (getInstance == nullptr || init == nullptr || enumerate == nullptr) {
        Stop();
        return {StatusCode::SymbolMissing, 0, "required TrackingClient symbol is missing"};
    }

    Dl_info info{};
    if (dladdr(getInstance, &info) == 0 || info.dli_fbase == nullptr) {
        Stop();
        return {StatusCode::RuntimeAbiMismatch, 0, "dladdr(getInstance) failed"};
    }

    impl_->diagnostics.libraryBase = reinterpret_cast<uintptr_t>(info.dli_fbase);
    impl_->diagnostics.libraryPath = info.dli_fname ? info.dli_fname : "";
    impl_->diagnostics.libraryBuildIdHex = ReadGnuBuildId(impl_->diagnostics.libraryPath);
    impl_->diagnostics.getBodyTrackerDataOffset = config.getBodyTrackerDataOffset;
    impl_->diagnostics.getBodyTrackerDataAddress =
        impl_->diagnostics.libraryBase + config.getBodyTrackerDataOffset;

    if (!config.expectedLibraryBuildIdHex.empty()) {
        const std::string expected = ToLower(config.expectedLibraryBuildIdHex);
        const std::string actual = ToLower(impl_->diagnostics.libraryBuildIdHex);
        if (actual.empty() || actual != expected) {
            const std::string detail = actual.empty() ? "unavailable" : actual;
            Stop();
            return {StatusCode::RuntimeBuildMismatch, 0,
                "libtrackingclient.pxr.so GNU Build ID mismatch; actual=" + detail};
        }
        impl_->diagnostics.runtimeIdentityVerified = true;
    } else if (!config.allowUnverifiedRuntime) {
        const std::string actual = impl_->diagnostics.libraryBuildIdHex;
        Stop();
        return {StatusCode::RuntimeBuildMismatch, 0,
            actual.empty()
                ? "runtime Build ID is unpinned; set expectedLibraryBuildIdHex or explicitly allow unverified runtime"
                : "runtime Build ID " + actual + " is unpinned; set expectedLibraryBuildIdHex or explicitly allow unverified runtime"};
    }

    void* bodyAddress = reinterpret_cast<void*>(impl_->diagnostics.getBodyTrackerDataAddress);
    if (!AddressIsExecutable(bodyAddress)) {
        Stop();
        return {StatusCode::RuntimeAbiMismatch, 0, "getBodyTrackerData offset does not resolve to executable memory"};
    }

    alignas(16) uintptr_t scratch[2] = {kScratch0, kScratch1};
    internal::TraceStartupEvent("runtime.get_instance.enter");
    const uintptr_t instanceRaw = pico_ot_call_noarg_with_sret_scratch(getInstance, scratch);
    internal::TraceStartupEvent("runtime.get_instance.return", instanceRaw != 0 ? 1 : 0);
    if (scratch[0] != kScratch0 || instanceRaw == 0) {
        Stop();
        return {StatusCode::RuntimeAbiMismatch, 0, "TrackingClient::getInstance ABI did not match recovered register-return behavior"};
    }

    impl_->instance = reinterpret_cast<void*>(instanceRaw);
    impl_->initTrackerMap = reinterpret_cast<InitTrackerMapFn>(init);
    impl_->getConnectState = reinterpret_cast<GetConnectStateFn>(enumerate);
    impl_->getBodyTrackerData = reinterpret_cast<GetBodyTrackerDataFn>(bodyAddress);

    internal::TraceStartupEvent("runtime.init_tracker_map.enter");
    impl_->initTrackerMap(impl_->instance);
    internal::TraceStartupEvent("runtime.init_tracker_map.return");
    impl_->started = true;
    internal::TraceStartupEvent("runtime.start.return");
    return Status::Ok();
#endif
}

void CurrentFirmwareRuntimeAdapter::Stop() {
    if (impl_ == nullptr) return;
    impl_->started = false;
    impl_->enumerationUsed = false;
    impl_->instance = nullptr;
    impl_->initTrackerMap = nullptr;
    impl_->getConnectState = nullptr;
    impl_->getBodyTrackerData = nullptr;
    if (impl_->library != nullptr) {
        dlclose(impl_->library);
        impl_->library = nullptr;
    }
    const uint64_t leaked = impl_->diagnostics.intentionallyLeakedEnumerationBytes;
    impl_->diagnostics = {};
    impl_->diagnostics.intentionallyLeakedEnumerationBytes = leaked;
}

Status CurrentFirmwareRuntimeAdapter::ReinitializeTrackerMap() {
    if (!impl_->started || impl_->initTrackerMap == nullptr || impl_->instance == nullptr) {
        return {StatusCode::RuntimeNotReady, 0, "runtime not started"};
    }
    impl_->initTrackerMap(impl_->instance);
    return Status::Ok();
}

Status CurrentFirmwareRuntimeAdapter::EnumerateTrackers(std::vector<DeviceInfo>& devices) {
    devices.clear();
    if (!impl_->started || impl_->getConnectState == nullptr || impl_->instance == nullptr) {
        return {StatusCode::RuntimeNotReady, 0, "runtime not started"};
    }
    if (impl_->enumerationUsed && !impl_->config.allowRepeatedLeakyEnumeration) {
        return {StatusCode::EnumerationRefreshDisabled, 0,
            "repeated TrackerInfo refresh is disabled because vendor vector deallocation is not proven safe"};
    }

    RawVector3Ptr raw{};
    internal::TraceStartupEvent("runtime.enumerate.enter");
    const int32_t rc = impl_->getConnectState(impl_->instance, &raw);
    internal::TraceStartupEvent("runtime.enumerate.vendor_return", rc);
    impl_->enumerationUsed = true;
    if (rc != 0) return {StatusCode::VendorError, rc, "getSwiftTrackerConnectState failed"};

    if (raw.begin == nullptr && raw.end == nullptr && raw.cap == nullptr) {
        internal::TraceStartupEvent("runtime.enumerate.vector_span_bytes", 0);
        internal::TraceStartupEvent("runtime.enumerate.vector_count", 0);
        internal::TraceStartupEvent("runtime.enumerate.accepted_count", 0);
        return Status::Ok();
    }
    if (raw.begin == nullptr || raw.end == nullptr || raw.cap == nullptr ||
        raw.begin > raw.end || raw.end > raw.cap) {
        return {StatusCode::RuntimeAbiMismatch, rc, "vendor TrackerInfo vector descriptor is invalid"};
    }

    const size_t span = static_cast<size_t>(raw.end - raw.begin);
    internal::TraceStartupEvent("runtime.enumerate.vector_span_bytes", static_cast<int64_t>(span));
    if ((span % kTrackerInfoSize) != 0) {
        return {StatusCode::RuntimeAbiMismatch, rc, "TrackerInfo vector span is not a multiple of 72 bytes"};
    }
    const size_t count = span / kTrackerInfoSize;
    internal::TraceStartupEvent("runtime.enumerate.vector_count", static_cast<int64_t>(count));
    if (count > kMaxTrackerInfos) {
        return {StatusCode::RuntimeAbiMismatch, rc, "TrackerInfo count exceeds recovered five-tracker bound"};
    }

    devices.reserve(count);
    const int64_t observedNs = MonotonicNowNs();
    for (size_t i = 0; i < count; ++i) {
        const uint8_t* entry = raw.begin + i * kTrackerInfoSize;
        DeviceInfo info{};
        info.serial = ReadSerial(entry);
        if (info.serial.empty()) continue;
        info.runtimeDeviceId = ReadI32(entry, 4);
        info.connected = ReadI32(entry, 0) == 1;
        info.charging = entry[42] != 0;
        info.batteryBucketRaw = entry[43];
        info.observedMonotonicNs = observedNs;
        devices.push_back(std::move(info));
    }

    // The vendor/system libc++ owns this allocation. The research handoff explicitly
    // forbids freeing it through an unproven app-side allocator ABI. One bounded refresh
    // is allowed by default; repeated refresh requires an explicit investigation opt-in.
    if (raw.begin != nullptr && raw.cap >= raw.begin) {
        impl_->diagnostics.intentionallyLeakedEnumerationBytes +=
            static_cast<uint64_t>(raw.cap - raw.begin);
    }
    internal::TraceStartupEvent("runtime.enumerate.accepted_count", static_cast<int64_t>(devices.size()));
    return Status::Ok();
}

Status CurrentFirmwareRuntimeAdapter::ReadTrackerPose(const std::string& serial, RawTrackerState& state) {
    if (!impl_->started || impl_->getBodyTrackerData == nullptr || impl_->instance == nullptr) {
        return {StatusCode::RuntimeNotReady, 0, "runtime not started"};
    }
    if (serial.empty()) return {StatusCode::InvalidArgument, 0, "serial is empty"};

    RawAlgoResultTracker a{};
    RawAlgoResultTracker b{};
    std::memset(a.bytes, kCanaryByte, sizeof(a.bytes));
    std::memset(b.bytes, kCanaryByte, sizeof(b.bytes));

    const int64_t observedNs = MonotonicNowNs();
    const std::string vendorSerial(serial);
    const int32_t rc = impl_->getBodyTrackerData(
        impl_->instance, vendorSerial, observedNs, a, b);

    if (!internal::OutputTailUnmodified(
            a.bytes, impl_->config.algoResultSize, sizeof(a.bytes), kCanaryByte) ||
        !internal::OutputTailUnmodified(
            b.bytes, impl_->config.algoResultSize, sizeof(b.bytes), kCanaryByte)) {
        return {StatusCode::RuntimeAbiMismatch, rc, "vendor wrote beyond the observed 392-byte result layout"};
    }

    if (rc != 0) return {StatusCode::VendorError, rc, "getBodyTrackerData failed"};

    state = {};
    state.serial = serial;
    return internal::DecodeOutputA(a.bytes, sizeof(a.bytes), rc, observedNs, state);
}

RuntimeDiagnostics CurrentFirmwareRuntimeAdapter::Diagnostics() const {
    return impl_->diagnostics;
}

std::unique_ptr<IPicoRuntimeAdapter> CreateCurrentFirmwareRuntimeAdapter() {
    return std::make_unique<CurrentFirmwareRuntimeAdapter>();
}

}  // namespace pico_ot
