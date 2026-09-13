#pragma once

#include <cstdint>

namespace pico_ot::bridge {

constexpr uint16_t kHmdIngressPort = 29765;
constexpr uint16_t kSteamVrFanoutPort = 29766;
constexpr uint16_t kMonakaFanoutPort = 29767;

constexpr uint32_t kBridgeServiceStatusMagic = 0x53544250u;  // "PBTS"
constexpr uint32_t kBridgeServiceStatusVersion = 1u;

enum class ServiceOutputRoute : uint32_t {
    SteamVr = 0,
    Monaka = 1,
    Both = 2,
    Disabled = 3,
};

struct BridgeServiceStatusSharedState {
    uint32_t magic = kBridgeServiceStatusMagic;
    uint32_t version = kBridgeServiceStatusVersion;
    uint64_t generation = 0;
    uint32_t route = static_cast<uint32_t>(ServiceOutputRoute::SteamVr);
    uint32_t trackerCount = 0;
    uint64_t activeSenderSessionId = 0;
    int64_t updatedPcMonotonicNs = 0;
};

#ifdef _WIN32
constexpr wchar_t kBridgeServiceInstanceMutexName[] =
    L"Local\\PicoOTBridge_ServiceInstance";
constexpr wchar_t kBridgeServiceStatusMappingName[] =
    L"Local\\PicoOTBridge_ServiceStatus";
constexpr wchar_t kBridgeOutputRouteUpdatedEventName[] =
    L"Local\\PicoOTBridge_OutputRouteUpdated";
#endif

constexpr const char* kBridgeOutputRouteConfigFileName = "output_route.txt";
constexpr const char* kRouteSteamVrValue = "steamvr";
constexpr const char* kRouteMonakaValue = "monaka";
constexpr const char* kRouteBothValue = "both";
constexpr const char* kRouteDisabledValue = "disabled";

}  // namespace pico_ot::bridge
