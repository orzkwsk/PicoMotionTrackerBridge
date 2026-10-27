#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/receiver.hpp"
#include "pico_ot_bridge/service_contract.hpp"
#include "pico_ot_backend/observation_identity.hpp"
#include "pico_ot_backend/observation_publisher.hpp"
#include "pico_ot_backend/runtime_diagnostic.hpp"
#include "pico_ot_backend/admission_diagnostic.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include "pico_ot_bridge/service_lifecycle.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
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

int main(int argc, char** argv) {
    const std::wstring lifecycleName = L"Local\\PicoOTBridge_ServiceLifecycle";
    if (argc == 2 && std::string(argv[1]) == "--stop")
        return pc_service_lifecycle::StopAndWait(lifecycleName, 5000);
    uint16_t observationPort = 29810;
    std::filesystem::path identityPath;
    std::string diagnosticPath;
    pico_ot::AdmissionConfig admissionConfig;
    DWORD trayOwner = 0;
    std::uint64_t trayOwnerCreated = 0;
    try {
        for (int i=1;i<argc;++i) {
            const std::string option=argv[i];
            if (option=="--version") {
                const auto build=pico_ot::RuntimeDiagnostic::Build();
                if(i+1<argc && std::string(argv[i+1])=="--json") std::cout << build.dump() << '\n';
                else std::cout << build["component_name"].get<std::string>() << ' ' << build["version"].get<std::string>() << ' ' << build["git_commit"].get<std::string>() << '\n';
                return 0;
            }
            if (option=="--help") {
                std::cout << "PICO PC service [--observation-port PORT] [--observation-config PATH] [--admission-config JSON_PATH] [--diagnostic-jsonl PATH] --tray-owner PID:FILETIME [--stop] [--version [--json]]\n";
                return 0;
            }
            if (i+1>=argc) throw std::invalid_argument("missing option value");
            const std::string value=argv[++i];
            if (option=="--observation-port") {
                if (value.empty() || value.find_first_not_of("0123456789")!=std::string::npos) throw std::invalid_argument("invalid port");
                const auto port=std::stoul(value);
                if (port==0 || port>65535 || (port>=29765 && port<=29768)) throw std::invalid_argument("invalid/reserved Observation port");
                observationPort=static_cast<uint16_t>(port);
            } else if (option=="--observation-config") identityPath=std::filesystem::u8path(value);
            else if(option=="--diagnostic-jsonl") diagnosticPath=value;
            else if(option=="--admission-config") {
                std::ifstream file(std::filesystem::u8path(value));
                if (!file) throw std::invalid_argument("cannot read admission config");
                admissionConfig=pico_ot::ParseAdmissionConfig(nlohmann::json::parse(file));
            }
            else if(option=="--tray-owner") pc_service_lifecycle::ParseOwner(value,trayOwner,trayOwnerCreated);
            else throw std::invalid_argument("unknown option");
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    if(!trayOwner) {
        std::cerr << "Start through the PICO Receiver tray. A process-lifetime owner is required.\n";
        return 1;
    }
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
    std::unique_ptr<pc_service_lifecycle::Endpoint> lifecycle;
    try {
        lifecycle = std::make_unique<pc_service_lifecycle::Endpoint>(lifecycleName, trayOwner, trayOwnerCreated);
    } catch (const std::exception& e) {
        std::cerr << "Service lifetime control unavailable: " << e.what() << '\n';
        ReleaseMutex(instanceMutex); CloseHandle(instanceMutex); return 6;
    }
    pico_ot::RuntimeDiagnostic diagnostic;
    if(!diagnosticPath.empty() && !diagnostic.Start(diagnosticPath))
        std::cerr << "Diagnostic output unavailable; production service continues.\n";

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
    receiverConfig.exclusiveBind = true;
    receiverConfig.rejectRetiredSessions = true;

    std::string error;
    if (!receiver.Start(receiverConfig, &error)) {
        diagnostic.Event("fatal_error",{{"operation","HMD ingress Start"},{"message",error}});
        std::cerr << "Failed to start HMD ingress receiver on UDP "
                  << receiverConfig.listenPort << ": " << error << '\n';
        CloseHandle(routeUpdatedEvent);
        CloseHandle(instanceMutex);
        return 4;
    }
    if(diagnostic.Enabled()) receiver.SetDiagnosticObserver([&] {
        diagnostic.Poll(receiver,pico_ot::bridge::MonotonicNowNs());
    });

    OutputSink steamVr{"SteamVR", pico_ot::bridge::kSteamVrFanoutPort};
    OutputSink monaka{"MonakaVR", pico_ot::bridge::kMonakaFanoutPort};
    OutputSink utility{"Utility", pico_ot::bridge::kUtilityFanoutPort};
    if (!StartSink(steamVr) || !StartSink(monaka) || !StartSink(utility)) {
        diagnostic.Event("fatal_error",{{"operation","fanout Start"}});
        receiver.Stop();
        CloseHandle(routeUpdatedEvent);
        ReleaseMutex(instanceMutex);
        CloseHandle(instanceMutex);
        return 5;
    }

    ServiceOutputRoute route = LoadRouteFromDisk();
    // Failure in the new candidate provider must not take down legacy outputs.
    std::unique_ptr<pico_ot::ObservationIdentity> observationIdentity;
    std::unique_ptr<pico_ot::PicoObservationAdapter> observationAdapter;
    pico_ot::ObservationPublisher observationPublisher;
    try {
        if (identityPath.empty()) {
            wchar_t local[32768]{};
            const auto length=GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768);
            if (length==0 || length>=32768) throw std::runtime_error("LOCALAPPDATA unavailable");
            identityPath=std::filesystem::path(local)/L"PicoMotionTrackerBridge"/L"observation-provider.conf";
        }
        observationIdentity=std::make_unique<pico_ot::ObservationIdentity>(identityPath);
        pico_ot::ObservationConfig config;
        config.sourceId=observationIdentity->SourceId(); config.spaceId=observationIdentity->SpaceId();
        config.spaceRevision=observationIdentity->Revision();
        config.clockEpochPcNs=pico_ot::bridge::MonotonicNowNs();
        config.newUuid=pico_ot::NewObservationUuid;
        config.sourceSpace=[&,lastSender=uint64_t(0),lastRevision=uint32_t(0),lastKnown=false](uint64_t sender) mutable {
            const auto state=observationIdentity->RefreshSpace(sender);
            if(sender!=lastSender||state.revision!=lastRevision||state.known!=lastKnown){
                std::cout<<"SOURCE_SPACE sender_session="<<sender<<" generation="<<observationIdentity->Generation()
                         <<" revision="<<state.revision<<" status="<<(state.known?"known":"unknown")<<std::endl;
                lastSender=sender;lastRevision=state.revision;lastKnown=state.known;
            }
            return state;
        };
        config.admission=admissionConfig;
        if (diagnostic.Enabled()) config.admissionDiagnostic=[&](const auto& raw,const auto& result,const auto& observation) {
            diagnostic.Event("pico_admission_sample",pico_ot::AdmissionDiagnosticJson(raw,result,observation),false);
        };
        observationAdapter=std::make_unique<pico_ot::PicoObservationAdapter>(std::move(config));
        if (!observationPublisher.Start(observationPort,&error)) throw std::runtime_error(error);
        std::cout << "C1 Observation -> 127.0.0.1:" << observationPort
                  << " source=" << observationIdentity->SourceId()
                  << " space=" << observationIdentity->SpaceId()
                  << " revision=" << observationIdentity->Revision() << '\n';
    } catch (const std::exception& e) {
        observationAdapter.reset();
        std::cerr << "C1 provider disabled; legacy outputs continue: " << e.what() << '\n';
    }
    std::cout << "PICO OT Bridge Service listening for HMD on UDP "
              << receiver.ListenPort() << '\n';
    std::cout << "Output route: " << RouteName(route) << '\n';
    std::cout << "Utility fan-out remains active on UDP "
              << pico_ot::bridge::kUtilityFanoutPort << " regardless of output route.\n";

    int64_t nextRoutePollNs = 0;
    int64_t nextStatusPublishNs = 0;
    bool receiverErrorLogged = false;
    diagnostic.Event("component_ready",{{"inventory_complete",false},{"inventory_boundary","accepted receiver packets only"},
        {"pico_admission",pico_ot::AdmissionConfigJson(admissionConfig)}});
    // Observe every accepted packet, including loss/reacquire inside one Pump.
    // Collect's sequence deduplication keeps the ordinary loop call harmless.
    if (admissionConfig.enabled || diagnostic.Enabled()) receiver.SetDiagnosticObserver([&] {
        diagnostic.Poll(receiver,pico_ot::bridge::MonotonicNowNs());
        if (admissionConfig.enabled && observationAdapter) {
            try { observationPublisher.Publish(*observationAdapter,receiver,pico_ot::bridge::MonotonicNowNs()); }
            catch (const std::exception& e) {
                diagnostic.Event("fatal_error",{{"operation","observation provider"},{"message",e.what()}});
                observationAdapter.reset();
            }
        }
    });

    lifecycle->MarkReady();
    while (gRun.load()) {
        if (lifecycle->StopRequested()) break;
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
                diagnostic.Event("diagnostic_warning",{{"operation","receiver Pump"},{"message",error}});
                receiverErrorLogged = true;
            }
        } else {
            receiverErrorLogged = false;
        }
        // Pump's packet callbacks may have observed receipts newer than the
        // loop's route-poll timestamp. Observe the updated cache with a new time.
        diagnostic.Poll(receiver,pico_ot::bridge::MonotonicNowNs());

        PumpSinkControl(steamVr);
        PumpSinkControl(monaka);
        PumpSinkControl(utility);
        ForwardLatest(receiver, steamVr, RouteIncludesSteamVr(route));
        ForwardLatest(receiver, monaka, RouteIncludesMonaka(route));
        ForwardLatest(receiver, utility, true);
        if (observationAdapter) {
            try {
                observationPublisher.Publish(*observationAdapter,receiver,pico_ot::bridge::MonotonicNowNs());
            } catch (const std::exception& e) {
                observationAdapter.reset();
                std::cerr << "C1 provider disabled; legacy outputs continue: " << e.what() << '\n';
            }
        }

        if (nowNs >= nextStatusPublishNs) {
            PublishStatus(status, route, receiver);
            nextStatusPublishNs = nowNs + 250'000'000LL;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    steamVr.publisher.Stop();
    std::cout << "C1 datagrams sent=" << observationPublisher.Sent()
              << " errors=" << observationPublisher.Errors() << '\n';
    monaka.publisher.Stop();
    utility.publisher.Stop();
    receiver.Stop();
    CloseHandle(routeUpdatedEvent);
    ReleaseMutex(instanceMutex);
    CloseHandle(instanceMutex);
    return 0;
}
