#pragma once

#include <cstdint>

namespace pico_ot::steamvr {

inline constexpr wchar_t kWorldAlignmentMappingName[] =
    L"Local\\PicoOTBridge_WorldAlignment";
inline constexpr wchar_t kWorldAlignmentUpdatedEventName[] =
    L"Local\\PicoOTBridge_WorldAlignmentUpdated";

inline constexpr std::uint32_t kWorldAlignmentMagic = 0x504F5441u; // POTA
inline constexpr std::uint32_t kWorldAlignmentVersion = 1u;

struct WorldAlignmentSharedState {
    std::uint32_t magic = kWorldAlignmentMagic;
    std::uint32_t version = kWorldAlignmentVersion;
    std::uint64_t generation = 0;
    double translationMeters[3] = {0.0, 0.0, 0.0};
};

static_assert(sizeof(WorldAlignmentSharedState) == 40,
              "WorldAlignmentSharedState ABI changed");

}  // namespace pico_ot::steamvr
