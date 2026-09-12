#include "device_provider.hpp"

#include <openvr_driver.h>

#include <cstring>

#if defined(_WIN32)
#define PICO_OT_DRIVER_EXPORT extern "C" __declspec(dllexport)
#elif defined(__GNUC__) || defined(__APPLE__)
#define PICO_OT_DRIVER_EXPORT extern "C" __attribute__((visibility("default")))
#else
#error Unsupported platform
#endif

namespace {
pico_ot::steamvr::DeviceProvider g_provider;
}

PICO_OT_DRIVER_EXPORT void* HmdDriverFactory(const char* interfaceName,
                                             int* returnCode) {
    if (interfaceName != nullptr &&
        std::strcmp(vr::IServerTrackedDeviceProvider_Version, interfaceName) == 0) {
        return &g_provider;
    }

    if (returnCode != nullptr) {
        *returnCode = vr::VRInitError_Init_InterfaceNotFound;
    }
    return nullptr;
}
