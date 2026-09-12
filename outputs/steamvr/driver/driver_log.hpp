#pragma once

#include <openvr_driver.h>

#include <cstdarg>
#include <cstdio>

namespace pico_ot::steamvr {

inline void DriverLog(const char* format, ...) {
    if (vr::VRDriverLog() == nullptr || format == nullptr) {
        return;
    }

    char buffer[1024]{};
    va_list args;
    va_start(args, format);
#ifdef _WIN32
    vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, args);
#else
    vsnprintf(buffer, sizeof(buffer), format, args);
#endif
    va_end(args);
    vr::VRDriverLog()->Log(buffer);
}

}  // namespace pico_ot::steamvr
