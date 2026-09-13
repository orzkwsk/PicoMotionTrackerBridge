#pragma once

namespace pico_ot::steamvr {

enum class OutputRoute {
    SteamVrDirect,
    External,
};

inline constexpr wchar_t kOutputRouteUpdatedEventName[] =
    L"Local\\PicoOTBridge_OutputRouteUpdated";
inline constexpr char kOutputRouteConfigFileName[] = "output_route.txt";
inline constexpr char kOutputRouteSteamVrDirectValue[] = "steamvr-direct";
inline constexpr char kOutputRouteExternalValue[] = "monaka-external";

}  // namespace pico_ot::steamvr
