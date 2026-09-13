#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/receiver.hpp"
#include "pico_ot_bridge/service_contract.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {

using pico_ot::bridge::BridgeServiceStatusSharedState;
using pico_ot::bridge::Publisher;
using pico_ot::bridge::PublisherConfig;
using pico_ot::bridge::ReceivedTrackerState;
using pico_ot::bridge::Receiver;
using pico_ot::bridge::ReceiverConfig;
using pico_ot::bridge::ServiceOutputRoute;

std::atomic<bool> gRun{true};

BOOL WINAPI ConsoleHandler(DWORD type) {
    switch (type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
    case CTRL_LOGOFF_EVENT:
    case CTRL_SHUTDOWN_EVENT:
        gRun.store(false);
        return TRUE;
    default:
        return FALSE;
    }
}

struct ForwardProgress {
    uint64_t sourceSessionId = 0;
    std::unordered_map<std::string, uint32_t> poseSequence;
    std::unordered_map<std::string, uint32_t> deviceSequence;

    void Reset() {
        sourceSessionId = 0;
        poseSequence.clear();
        deviceSequence.clear();
    }
};

struct OutputSink {
    const char* name = nullptr;
    uint16_t port = 0;
    Publisher publisher;
    ForwardProgress progress;
    bool errorLogged = false;
};

struct StatusMapping {
    HANDLE mapping = nullptr;
    BridgeServiceStatusSharedState* state = nullptr;

    ~StatusMapping() {
        if (state != nullptr) {
            UnmapViewOfFile(state);
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
    }
};

std::string TrimLower(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(
        std::find_if(value.rbegin(), value.rend(), notSpace).base(),
        value.end());
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

std::string RouteConfigPath() {
    char localAppData[32768]{};
    const DWORD length = GetEnvironmentVariableA(
        "LOCALAPPDATA",
        localAppData,
        static_cast<DWORD>(sizeof(localAppData)));
    if (length == 0 || length >= sizeof(localAppData)) {
        return {};
    }

    std::string path(localAppData, length);
    path += "\\PicoMotionTrackerBridge\\";
    path += pico_ot::bridge::kBridgeOutputRouteConfigFileName;
    return path;
}

ServiceOutputRoute LoadRouteFromDisk() {
    const std::string path = RouteConfigPath();
    if (path.empty()) {
        return ServiceOutputRoute::SteamVr;
    }

    std::ifstream input(path);
    if (!input) {
        return ServiceOutputRoute::SteamVr;
    }

    std::string value;
    std::getline(input, value);
    value = TrimLower(std::move(value));

    if (value == pico_ot::bridge::kRouteMonakaValue ||
        value == "monaka-external" ||
        value == "external") {
        return ServiceOutputRoute::Monaka;
    }
    if (value == pico_ot::bridge::kRouteBothValue) {
        return ServiceOutputRoute::Both;
    }
    if (value == pico_ot::bridge::kRouteDisabledValue || value == "off") {
        return ServiceOutputRoute::Disabled;
    }
    return ServiceOutputRoute::SteamVr;
}

const char* RouteName(ServiceOutputRoute route) {
    switch (route) {
    case ServiceOutputRoute::SteamVr:
        return "SteamVR";
    case ServiceOutputRoute::Monaka:
        return "MonakaVR";
    case ServiceOutputRoute::Both:
        return "Both";
    case ServiceOutputRoute::Disabled:
        return "Disabled";
    default:
        return "Unknown";
    }
}

bool RouteIncludesSteamVr(ServiceOutputRoute route) {
    return route == ServiceOutputRoute::SteamVr || route == ServiceOutputRoute::Both;
}

bool RouteIncludesMonaka(ServiceOutputRoute route) {
    return route == ServiceOutputRoute::Monaka || route == ServiceOutputRoute::Both;
}

pico_ot::TrackerState ToTrackerState(const ReceivedTrackerState& received) {
    pico_ot::TrackerState state;
    const auto& pose = received.pose;
    state.serial = pose.serial;
    state.runtimeDeviceId = pose.runtimeDeviceId;
    state.connectedSnapshot = pose.connectedSnapshot;
    state.positionValid = pose.positionValid;
    state.orientationSamplePresent = pose.orientationSamplePresent;
    state.rawTrackingState65 = pose.rawTrackingState65;
    state.sourceTimestamp = pose.sourceTimestamp;

    const int64_t pcPoseNs = received.posePcMonotonicNs.has_value()
                                 ? *received.posePcMonotonicNs
                                 : received.lastPoseReceivePcNs;
    state.observedMonotonicNs = pcPoseNs;
    state.mappedMonotonicNs = pcPoseNs;
    state.mappedMonotonicValid = pcPoseNs > 0;

    state.positionMeters = {
        pose.positionMeters.x,
        pose.positionMeters.y,
        pose.positionMeters.z};
    state.orientationXyzw = {
        pose.orientationXyzw.x,
        pose.orientationXyzw.y,
        pose.orientationXyzw.z,
        pose.orientationXyzw.w};
    state.linearVelocityMetersPerSec = {
        pose.linearVelocityMetersPerSec.x,
        pose.linearVelocityMetersPerSec.y,
        pose.linearVelocityMetersPerSec.z};
    state.angularVelocityRadPerSec = {
        pose.angularVelocityRadPerSec.x,
        pose.angularVelocityRadPerSec.y,
        pose.angularVelocityRadPerSec.z};
    state.frame = static_cast<pico_ot::ReferenceFrame>(pose.referenceFrame);
    state.angularVelocityFrame =
        static_cast<pico_ot::AngularVelocityFrame>(pose.angularVelocityFrame);
    return state;
}

pico_ot::DeviceInfo ToDeviceInfo(const ReceivedTrackerState& received) {
    pico_ot::DeviceInfo device;
    if (!received.deviceState.has_value()) {
        return device;
    }

    const auto& source = *received.deviceState;
    device.serial = source.serial;
    device.runtimeDeviceId = source.runtimeDeviceId;
    device.connected = source.connectedSnapshot;
    device.charging = source.charging;
    if (source.batteryBucketPresent) {
        device.batteryBucketRaw = source.batteryBucketRaw;
    }
    device.observedMonotonicNs = received.lastDeviceReceivePcNs;
    return device;
}

bool StartSink(OutputSink& sink) {
    PublisherConfig config;
    config.pcEndpoint = {"127.0.0.1", sink.port};
    config.localPort = 0;

    std::string error;
    if (!sink.publisher.Start(config, &error)) {
        std::cerr << "Failed to start " << sink.name << " fan-out publisher: "
                  << error << '\n';
        return false;
    }

    std::cout << sink.name << " fan-out -> 127.0.0.1:" << sink.port
              << " from local UDP " << sink.publisher.LocalPort() << '\n';
    return true;
}

void PumpSinkControl(OutputSink& sink) {
    std::string error;
    if (sink.publisher.PumpControl(&error)) {
        sink.errorLogged = false;
        return;
    }
    if (!sink.errorLogged) {
        std::cerr << sink.name << " control error: " << error << '\n';
        sink.errorLogged = true;
    }
}

void ForwardLatest(Receiver& receiver, OutputSink& sink, bool enabled) {
    if (!enabled) {
        return;
    }

    const uint64_t sourceSessionId = receiver.ActiveSenderSessionId();
    if (sourceSessionId != sink.progress.sourceSessionId) {
        sink.progress.Reset();
        sink.progress.sourceSessionId = sourceSessionId;
    }

    for (const auto& serial : receiver.Serials()) {
        const ReceivedTrackerState* received = receiver.Find(serial);
        if (received == nullptr) {
            continue;
        }

        std::string error;
        if (received->hasPoseSequence) {
            const auto it = sink.progress.poseSequence.find(serial);
            const bool unsent =
                it == sink.progress.poseSequence.end() ||
                it->second != received->lastPoseSequence;
            if (unsent) {
                const pico_ot::TrackerState state = ToTrackerState(*received);
                if (sink.publisher.SendPose(state, &error)) {
                    sink.progress.poseSequence[serial] = received->lastPoseSequence;
                    sink.errorLogged = false;
                } else if (!sink.errorLogged) {
                    std::cerr << sink.name << " pose forward failed: " << error << '\n';
                    sink.errorLogged = true;
                }
            }
        }

        if (received->hasDeviceSequence && received->deviceState.has_value()) {
            const auto it = sink.progress.deviceSequence.find(serial);
            const bool unsent =
                it == sink.progress.deviceSequence.end() ||
                it->second != received->lastDeviceSequence;
            if (unsent) {
                const pico_ot::DeviceInfo device = ToDeviceInfo(*received);
                if (sink.publisher.SendDeviceState(device, &error)) {
                    sink.progress.deviceSequence[serial] = received->lastDeviceSequence;
                    sink.errorLogged = false;
                } else if (!sink.errorLogged) {
                    std::cerr << sink.name << " device-state forward failed: " << error << '\n';
                    sink.errorLogged = true;
                }
            }
        }
    }
}

bool OpenStatusMapping(StatusMapping& status) {
    status.mapping = CreateFileMappingW(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(sizeof(BridgeServiceStatusSharedState)),
        pico_ot::bridge::kBridgeServiceStatusMappingName);
    if (status.mapping == nullptr) {
        std::cerr << "Failed to create service status mapping: " << GetLastError() << '\n';
        return false;
    }

    status.state = static_cast<BridgeServiceStatusSharedState*>(
        MapViewOfFile(
            status.mapping,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(BridgeServiceStatusSharedState)));
    if (status.state == nullptr) {
        std::cerr << "Failed to map service status: " << GetLastError() << '\n';
        return false;
    }

    std::memset(status.state, 0, sizeof(*status.state));
    status.state->magic = pico_ot::bridge::kBridgeServiceStatusMagic;
    status.state->version = pico_ot::bridge::kBridgeServiceStatusVersion;
    return true;
}

void PublishStatus(StatusMapping& status,
                   ServiceOutputRoute route,
                   const Receiver& receiver) {
    if (status.state == nullptr) {
        return;
    }

    const auto serials = receiver.Serials();
    status.state->route = static_cast<uint32_t>(route);
    status.state->trackerCount = static_cast<uint32_t>(serials.size());
    status.state->activeSenderSessionId = receiver.ActiveSenderSessionId();
    status.state->updatedPcMonotonicNs = pico_ot::bridge::MonotonicNowNs();
    MemoryBarrier();
    ++status.state->generation;
}

}  // namespace

int main() {
    HANDLE instanceMutex = CreateMutexW(
        nullptr,
        TRUE,
        pico_ot::bridge::kBridgeServiceInstanceMutexName);
    if (instanceMutex == nullptr) {
        std::cerr << "Failed to create service instance mutex: " << GetLastError() << '\n';
        return 2;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::cout << "PICO OT Bridge Service is already running.\n";
        CloseHandle(instanceMutex);
        return 0;
    }

    SetConsoleCtrlHandler(ConsoleHandler, TRUE);

    HANDLE routeUpdatedEvent = CreateEventW(
        nullptr,
        FALSE,
        FALSE,
        pico_ot::bridge::kBridgeOutputRouteUpdatedEventName);
    if (routeUpdatedEvent == nullptr) {
        std::cerr << "Failed to create output-route event: " << GetLastError() << '\n';
        CloseHandle(instanceMutex);
        return 3;
    }

    StatusMapping status;
    OpenStatusMapping(status);

    Receiver receiver;
    ReceiverConfig receiverConfig;
    receiverConfig.listenPort = pico_ot::bridge::kHmdIngressPort;
    receiverConfig.timeSyncIntervalNs = 1'000'000'000LL;
    receiverConfig.staleTimeoutNs = 500'000'000LL;

    std::string error;
    if (!receiver.Start(receiverConfig, &error)) {
        std::cerr << "Failed to start HMD ingress receiver on UDP "
                  << receiverConfig.listenPort << ": " << error << '\n';
        CloseHandle(routeUpdatedEvent);
        CloseHandle(instanceMutex);
        return 4;
    }

    OutputSink steamVr{"SteamVR", pico_ot::bridge::kSteamVrFanoutPort};
    OutputSink monaka{"MonakaVR", pico_ot::bridge::kMonakaFanoutPort};
    OutputSink utility{"Utility", pico_ot::bridge::kUtilityFanoutPort};
    if (!StartSink(steamVr) || !StartSink(monaka) || !StartSink(utility)) {
        receiver.Stop();
        CloseHandle(routeUpdatedEvent);
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
        return 5;
    }

    ServiceOutputRoute route = LoadRouteFromDisk();
    std::cout << "PICO OT Bridge Service listening for HMD on UDP "
              << receiver.ListenPort() << '\n';
    std::cout << "Output route: " << RouteName(route) << '\n';
    std::cout << "Utility fan-out remains active on UDP "
              << pico_ot::bridge::kUtilityFanoutPort << " regardless of output route.\n";

    int64_t nextRoutePollNs = 0;
    int64_t nextStatusPublishNs = 0;
    bool receiverErrorLogged = false;

    while (gRun.load()) {
        const int64_t nowNs = pico_ot::bridge::MonotonicNowNs();

        bool reloadRoute = false;
        if (WaitForSingleObject(routeUpdatedEvent, 0) == WAIT_OBJECT_0) {
            reloadRoute = true;
        }
        if (nowNs >= nextRoutePollNs) {
            reloadRoute = true;
            nextRoutePollNs = nowNs + 1'000'000'000LL;
        }
        if (reloadRoute) {
            const ServiceOutputRoute requested = LoadRouteFromDisk();
            if (requested != route) {
                route = requested;
                steamVr.progress.Reset();
                monaka.progress.Reset();
                std::cout << "Output route changed: " << RouteName(route) << '\n';
            }
        }

        error.clear();
        if (!receiver.Pump(&error)) {
            if (!receiverErrorLogged) {
                std::cerr << "HMD ingress receiver error: " << error << '\n';
                receiverErrorLogged = true;
            }
        } else {
            receiverErrorLogged = false;
        }

        PumpSinkControl(steamVr);
        PumpSinkControl(monaka);
        PumpSinkControl(utility);
        ForwardLatest(receiver, steamVr, RouteIncludesSteamVr(route));
        ForwardLatest(receiver, monaka, RouteIncludesMonaka(route));
        ForwardLatest(receiver, utility, true);

        if (nowNs >= nextStatusPublishNs) {
            PublishStatus(status, route, receiver);
            nextStatusPublishNs = nowNs + 250'000'000LL;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    steamVr.publisher.Stop();
    monaka.publisher.Stop();
    utility.publisher.Stop();
    receiver.Stop();
    CloseHandle(routeUpdatedEvent);
    ReleaseMutex(instanceMutex);
    CloseHandle(instanceMutex);
    return 0;
}
