#include "device_provider.hpp"

#include "driver_log.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
#include "world_alignment_ipc.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <cmath>
#include <cstring>
#include <utility>

namespace pico_ot::steamvr {
namespace {

constexpr wchar_t kOrientationZeroEventName[] =
    L"Local\\PicoOTBridge_OrientationZero";
constexpr wchar_t kOrientationClearEventName[] =
    L"Local\\PicoOTBridge_OrientationClear";
constexpr wchar_t kPositionZeroEventName[] =
    L"Local\\PicoOTBridge_PositionZero";
constexpr wchar_t kPositionClearEventName[] =
    L"Local\\PicoOTBridge_PositionClear";
constexpr wchar_t kRenderModelShowEventName[] =
    L"Local\\PicoOTBridge_RenderModelShow";
constexpr wchar_t kRenderModelHideEventName[] =
    L"Local\\PicoOTBridge_RenderModelHide";

bool TranslationIsFinite(const double translation[3]) {
    return std::isfinite(translation[0]) &&
           std::isfinite(translation[1]) &&
           std::isfinite(translation[2]);
}

}  // namespace

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

    orientationZeroEvent_ = CreateEventW(
        nullptr, FALSE, FALSE, kOrientationZeroEventName);
    if (orientationZeroEvent_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create orientation-zero event: %lu",
                  static_cast<unsigned long>(GetLastError()));
    }

    orientationClearEvent_ = CreateEventW(
        nullptr, FALSE, FALSE, kOrientationClearEventName);
    if (orientationClearEvent_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create orientation-clear event: %lu",
                  static_cast<unsigned long>(GetLastError()));
    }

    positionZeroEvent_ = CreateEventW(
        nullptr, FALSE, FALSE, kPositionZeroEventName);
    if (positionZeroEvent_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create position-zero event: %lu",
                  static_cast<unsigned long>(GetLastError()));
    }

    positionClearEvent_ = CreateEventW(
        nullptr, FALSE, FALSE, kPositionClearEventName);
    if (positionClearEvent_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create position-clear event: %lu",
                  static_cast<unsigned long>(GetLastError()));
    }

    renderModelShowEvent_ = CreateEventW(
        nullptr, FALSE, FALSE, kRenderModelShowEventName);
    if (renderModelShowEvent_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create render-model-show event: %lu",
                  static_cast<unsigned long>(GetLastError()));
    }

    renderModelHideEvent_ = CreateEventW(
        nullptr, FALSE, FALSE, kRenderModelHideEventName);
    if (renderModelHideEvent_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create render-model-hide event: %lu",
                  static_cast<unsigned long>(GetLastError()));
    }

    worldAlignmentMapping_ = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(WorldAlignmentSharedState)),
        kWorldAlignmentMappingName);
    const DWORD mappingCreateError = GetLastError();
    if (worldAlignmentMapping_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create world-alignment mapping: %lu",
                  static_cast<unsigned long>(mappingCreateError));
    } else {
        worldAlignmentView_ = MapViewOfFile(
            static_cast<HANDLE>(worldAlignmentMapping_),
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(WorldAlignmentSharedState));
        if (worldAlignmentView_ == nullptr) {
            DriverLog("pico_ot_bridge: failed to map world-alignment state: %lu",
                      static_cast<unsigned long>(GetLastError()));
        } else {
            auto* shared =
                static_cast<WorldAlignmentSharedState*>(worldAlignmentView_);
            const bool existingMapping =
                mappingCreateError == ERROR_ALREADY_EXISTS;
            if (!existingMapping ||
                shared->magic != kWorldAlignmentMagic ||
                shared->version != kWorldAlignmentVersion ||
                !TranslationIsFinite(shared->translationMeters)) {
                std::memset(shared, 0, sizeof(*shared));
                shared->magic = kWorldAlignmentMagic;
                shared->version = kWorldAlignmentVersion;
            } else {
                worldTranslationMeters_[0] = shared->translationMeters[0];
                worldTranslationMeters_[1] = shared->translationMeters[1];
                worldTranslationMeters_[2] = shared->translationMeters[2];
            }
        }
    }

    worldAlignmentUpdatedEvent_ = CreateEventW(
        nullptr, FALSE, FALSE, kWorldAlignmentUpdatedEventName);
    if (worldAlignmentUpdatedEvent_ == nullptr) {
        DriverLog("pico_ot_bridge: failed to create world-alignment event: %lu",
                  static_cast<unsigned long>(GetLastError()));
    }

    DriverLog("pico_ot_bridge: SteamVR provider listening on UDP %u",
              receiver_.ListenPort());
    DriverLog("pico_ot_bridge: orientation diagnostics ready (Zero/Clear named events)");
    DriverLog("pico_ot_bridge: position diagnostics ready (Zero/Clear named events)");
    DriverLog("pico_ot_bridge: render-model controls ready (Show/Hide named events)");
    DriverLog(
        "pico_ot_bridge: world alignment ready translation=%.6f %.6f %.6f m",
        worldTranslationMeters_[0],
        worldTranslationMeters_[1],
        worldTranslationMeters_[2]);
    return vr::VRInitError_None;
}

void DeviceProvider::Cleanup() {
    if (orientationZeroEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(orientationZeroEvent_));
        orientationZeroEvent_ = nullptr;
    }
    if (orientationClearEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(orientationClearEvent_));
        orientationClearEvent_ = nullptr;
    }
    if (positionZeroEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(positionZeroEvent_));
        positionZeroEvent_ = nullptr;
    }
    if (positionClearEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(positionClearEvent_));
        positionClearEvent_ = nullptr;
    }
    if (renderModelShowEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(renderModelShowEvent_));
        renderModelShowEvent_ = nullptr;
    }
    if (renderModelHideEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(renderModelHideEvent_));
        renderModelHideEvent_ = nullptr;
    }
    if (worldAlignmentUpdatedEvent_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(worldAlignmentUpdatedEvent_));
        worldAlignmentUpdatedEvent_ = nullptr;
    }
    if (worldAlignmentView_ != nullptr) {
        UnmapViewOfFile(worldAlignmentView_);
        worldAlignmentView_ = nullptr;
    }
    if (worldAlignmentMapping_ != nullptr) {
        CloseHandle(static_cast<HANDLE>(worldAlignmentMapping_));
        worldAlignmentMapping_ = nullptr;
    }

    diagnosticOrientationZeroEnabled_ = false;
    diagnosticPositionZeroEnabled_ = false;
    renderModelVisible_ = true;
    worldTranslationMeters_[0] = 0.0;
    worldTranslationMeters_[1] = 0.0;
    worldTranslationMeters_[2] = 0.0;
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

        rawDevice->SetWorldTranslation(
            worldTranslationMeters_[0],
            worldTranslationMeters_[1],
            worldTranslationMeters_[2]);
        rawDevice->SetRenderModelVisible(renderModelVisible_);

        if (diagnosticOrientationZeroEnabled_) {
            rawDevice->RequestOrientationZero();
        }
        if (diagnosticPositionZeroEnabled_) {
            rawDevice->RequestPositionZero();
        }

        devices_.emplace(serial, std::move(device));
        DriverLog("pico_ot_bridge: registered Generic Tracker %s",
                  serial.c_str());
    }
}

void DeviceProvider::ProcessDiagnosticControls() {
    if (orientationZeroEvent_ != nullptr &&
        WaitForSingleObject(static_cast<HANDLE>(orientationZeroEvent_), 0) ==
            WAIT_OBJECT_0) {
        diagnosticOrientationZeroEnabled_ = true;
        for (auto& entry : devices_) {
            entry.second->RequestOrientationZero();
        }
        DriverLog("pico_ot_bridge: diagnostic orientation zero requested for all trackers");
    }

    if (orientationClearEvent_ != nullptr &&
        WaitForSingleObject(static_cast<HANDLE>(orientationClearEvent_), 0) ==
            WAIT_OBJECT_0) {
        diagnosticOrientationZeroEnabled_ = false;
        for (auto& entry : devices_) {
            entry.second->ClearOrientationZero();
        }
        DriverLog("pico_ot_bridge: diagnostic orientation zero cleared for all trackers");
    }

    if (positionZeroEvent_ != nullptr &&
        WaitForSingleObject(static_cast<HANDLE>(positionZeroEvent_), 0) ==
            WAIT_OBJECT_0) {
        diagnosticPositionZeroEnabled_ = true;
        for (auto& entry : devices_) {
            entry.second->RequestPositionZero();
        }
        DriverLog("pico_ot_bridge: diagnostic position zero requested for all trackers");
    }

    if (positionClearEvent_ != nullptr &&
        WaitForSingleObject(static_cast<HANDLE>(positionClearEvent_), 0) ==
            WAIT_OBJECT_0) {
        diagnosticPositionZeroEnabled_ = false;
        for (auto& entry : devices_) {
            entry.second->ClearPositionZero();
        }
        DriverLog("pico_ot_bridge: diagnostic position zero cleared for all trackers");
    }
}

void DeviceProvider::ProcessRenderModelControls() {
    if (renderModelShowEvent_ != nullptr &&
        WaitForSingleObject(static_cast<HANDLE>(renderModelShowEvent_), 0) ==
            WAIT_OBJECT_0) {
        renderModelVisible_ = true;
        ApplyRenderModelVisibilityToDevices();
        DriverLog("pico_ot_bridge: render models shown for all trackers");
    }

    if (renderModelHideEvent_ != nullptr &&
        WaitForSingleObject(static_cast<HANDLE>(renderModelHideEvent_), 0) ==
            WAIT_OBJECT_0) {
        renderModelVisible_ = false;
        ApplyRenderModelVisibilityToDevices();
        DriverLog("pico_ot_bridge: render models hidden for all trackers");
    }
}

void DeviceProvider::ProcessWorldAlignmentControl() {
    if (worldAlignmentUpdatedEvent_ == nullptr ||
        worldAlignmentView_ == nullptr ||
        WaitForSingleObject(
            static_cast<HANDLE>(worldAlignmentUpdatedEvent_), 0) != WAIT_OBJECT_0) {
        return;
    }

    const auto* shared =
        static_cast<const WorldAlignmentSharedState*>(worldAlignmentView_);
    if (shared->magic != kWorldAlignmentMagic ||
        shared->version != kWorldAlignmentVersion) {
        DriverLog(
            "pico_ot_bridge: rejected world alignment ABI magic=%08x version=%u",
            static_cast<unsigned int>(shared->magic),
            static_cast<unsigned int>(shared->version));
        return;
    }
    if (!TranslationIsFinite(shared->translationMeters)) {
        DriverLog("pico_ot_bridge: rejected non-finite world translation");
        return;
    }

    worldTranslationMeters_[0] = shared->translationMeters[0];
    worldTranslationMeters_[1] = shared->translationMeters[1];
    worldTranslationMeters_[2] = shared->translationMeters[2];
    ApplyWorldTranslationToDevices();

    DriverLog(
        "pico_ot_bridge: world translation updated gen=%llu t=%.6f %.6f %.6f m",
        static_cast<unsigned long long>(shared->generation),
        worldTranslationMeters_[0],
        worldTranslationMeters_[1],
        worldTranslationMeters_[2]);
}

void DeviceProvider::ApplyWorldTranslationToDevices() {
    for (auto& entry : devices_) {
        entry.second->SetWorldTranslation(
            worldTranslationMeters_[0],
            worldTranslationMeters_[1],
            worldTranslationMeters_[2]);
    }
}

void DeviceProvider::ApplyRenderModelVisibilityToDevices() {
    for (auto& entry : devices_) {
        entry.second->SetRenderModelVisible(renderModelVisible_);
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
    ProcessDiagnosticControls();
    ProcessRenderModelControls();
    ProcessWorldAlignmentControl();
    UpdateTrackers(pico_ot::bridge::MonotonicNowNs());

    // Drain runtime events so they do not accumulate even though the initial
    // tracker implementation has no event-specific behavior.
    vr::VREvent_t event{};
    while (vr::VRServerDriverHost()->PollNextEvent(&event, sizeof(event))) {
    }
}

}  // namespace pico_ot::steamvr
