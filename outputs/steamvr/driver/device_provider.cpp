#include "device_provider.hpp"

#include "driver_log.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"

#include <utility>

namespace pico_ot::steamvr {

vr::EVRInitError DeviceProvider::Init(vr::IVRDriverContext* driverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(driverContext);

    pico_ot::bridge::ReceiverConfig config;
    config.listenPort = 29765;
    config.timeSyncIntervalNs = 1'000'000'000LL;
    config.staleTimeoutNs = 500'000'000LL;

    std::string error;
    if (!receiver_.Start(config, &error)) {
        DriverLog("pico_ot_bridge: receiver start failed: %s", error.c_str());
        return vr::VRInitError_Driver_Unknown;
    }

    DriverLog("pico_ot_bridge: SteamVR provider listening on UDP %u",
              receiver_.ListenPort());
    return vr::VRInitError_None;
}

void DeviceProvider::Cleanup() {
    devices_.clear();
    receiver_.Stop();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

const char* const* DeviceProvider::GetInterfaceVersions() {
    return vr::k_InterfaceVersions;
}

bool DeviceProvider::ShouldBlockStandbyMode() {
    return false;
}

void DeviceProvider::EnterStandby() {}
void DeviceProvider::LeaveStandby() {}

void DeviceProvider::AddNewTrackers() {
    for (const auto& serial : receiver_.Serials()) {
        if (devices_.find(serial) != devices_.end()) {
            continue;
        }

        const auto* state = receiver_.Find(serial);
        if (state == nullptr || !state->hasPoseSequence) {
            continue;
        }

        auto device = std::make_unique<TrackerDevice>(serial);
        TrackerDevice* rawDevice = device.get();
        if (!vr::VRServerDriverHost()->TrackedDeviceAdded(
                serial.c_str(), vr::TrackedDeviceClass_GenericTracker, rawDevice)) {
            DriverLog("pico_ot_bridge: TrackedDeviceAdded failed for %s",
                      serial.c_str());
            continue;
        }

        devices_.emplace(serial, std::move(device));
        DriverLog("pico_ot_bridge: registered Generic Tracker %s",
                  serial.c_str());
    }
}

void DeviceProvider::UpdateTrackers(int64_t nowPcMonotonicNs) {
    for (auto& entry : devices_) {
        const auto* state = receiver_.Find(entry.first);
        const bool stale = state == nullptr ||
                           receiver_.IsPoseStale(*state, nowPcMonotonicNs);
        entry.second->Update(state, stale, nowPcMonotonicNs);
    }
}

void DeviceProvider::RunFrame() {
    std::string error;
    if (!receiver_.Pump(&error)) {
        if (!receiverErrorLogged_) {
            DriverLog("pico_ot_bridge: receiver error: %s", error.c_str());
            receiverErrorLogged_ = true;
        }
    } else {
        receiverErrorLogged_ = false;
    }

    AddNewTrackers();
    UpdateTrackers(pico_ot::bridge::MonotonicNowNs());

    // Drain runtime events so they do not accumulate even though the initial
    // tracker implementation has no event-specific behavior.
    vr::VREvent_t event{};
    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
    }
}

}  // namespace pico_ot::steamvr
