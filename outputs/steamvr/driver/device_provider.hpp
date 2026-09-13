#pragma once

#include "output_route_ipc.hpp"
#include "pico_ot_bridge/receiver.hpp"
#include "tracker_device.hpp"

#include <openvr_driver.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace pico_ot::steamvr {

class DeviceProvider final : public vr::IServerTrackedDeviceProvider {
public:
    vr::EVRInitError Init(vr::IVRDriverContext* driverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override;
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override;
    void EnterStandby() override;
    void LeaveStandby() override;

private:
    void AddNewTrackers();
    void ProcessDiagnosticControls();
    void ProcessWorldAlignmentControl();
    void ProcessOutputRouteControl(int64_t nowPcMonotonicNs);
    void EnsureReceiverRunning(int64_t nowPcMonotonicNs);
    void SetOutputRoute(OutputRoute route, int64_t nowPcMonotonicNs);
    void ApplyWorldTranslationToDevices();
    void UpdateTrackers(int64_t nowPcMonotonicNs);

    pico_ot::bridge::Receiver receiver_;
    pico_ot::bridge::ReceiverConfig receiverConfig_{};
    std::unordered_map<std::string, std::unique_ptr<TrackerDevice>> devices_;
    bool receiverErrorLogged_ = false;
    int64_t nextReceiverStartAttemptNs_ = 0;
    OutputRoute outputRoute_ = OutputRoute::SteamVrDirect;

    // Win32 named-event handles stored as opaque pointers to avoid leaking
    // Windows headers into the public driver header.
    void* outputRouteUpdatedEvent_ = nullptr;
    void* orientationZeroEvent_ = nullptr;
    void* orientationClearEvent_ = nullptr;
    void* positionZeroEvent_ = nullptr;
    void* positionClearEvent_ = nullptr;
    bool diagnosticOrientationZeroEnabled_ = false;
    bool diagnosticPositionZeroEnabled_ = false;

    // Shared world-alignment IPC. The driver owns the mapping lifetime while
    // SteamVR is running; a PC-side calibrator updates the state and signals
    // worldAlignmentUpdatedEvent_.
    void* worldAlignmentMapping_ = nullptr;
    void* worldAlignmentView_ = nullptr;
    void* worldAlignmentUpdatedEvent_ = nullptr;
    double worldTranslationMeters_[3] = {0.0, 0.0, 0.0};
};

}  // namespace pico_ot::steamvr
