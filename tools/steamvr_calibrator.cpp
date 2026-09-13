#include "world_alignment_ipc.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <openvr.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <optional>
#include <string>
#include <thread>

namespace {

enum class ReferenceKind {
    None,
    LeftController,
    RightController,
    Hmd,
};

struct Options {
    bool list = false;
    bool listTrackers = false;
    bool clear = false;
    bool measureOnly = false;
    std::string trackerSerial;
    ReferenceKind reference = ReferenceKind::None;
    int samples = 120;
    int intervalMs = 8;
};

struct AlignmentMapping {
    HANDLE mapping = nullptr;
    HANDLE updatedEvent = nullptr;
    pico_ot::steamvr::WorldAlignmentSharedState* state = nullptr;

    ~AlignmentMapping() {
        if (state != nullptr) {
            UnmapViewOfFile(state);
        }
        if (updatedEvent != nullptr) {
            CloseHandle(updatedEvent);
        }
        if (mapping != nullptr) {
            CloseHandle(mapping);
        }
    }
};

void PrintUsage() {
    std::cout
        << "PICO OT SteamVR translation calibrator\n\n"
        << "Usage:\n"
        << "  pico_ot_steamvr_calibrator --list\n"
        << "  pico_ot_steamvr_calibrator --list-trackers\n"
        << "  pico_ot_steamvr_calibrator --clear\n"
        << "  pico_ot_steamvr_calibrator --tracker <serial> --reference <left|right|hmd>\n"
        << "      [--measure-only] [--samples <count>] [--interval-ms <milliseconds>]\n\n"
        << "Backward compatibility:\n"
        << "  --controller <left|right> is accepted as an alias for --reference.\n\n"
        << "Calibration assumes the selected reference device and PICO tracker pose\n"
        << "reference points are held at the same physical point while sampling.\n"
        << "--measure-only calculates the suggested translation without applying it.\n";
}

bool ParsePositiveInt(const char* text, int& value) {
    if (text == nullptr || *text == '\0') {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || parsed <= 0 || parsed > 100000) {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseReference(const std::string& text, ReferenceKind& reference) {
    if (text == "left") {
        reference = ReferenceKind::LeftController;
        return true;
    }
    if (text == "right") {
        reference = ReferenceKind::RightController;
        return true;
    }
    if (text == "hmd") {
        reference = ReferenceKind::Hmd;
        return true;
    }
    return false;
}

bool ParseArgs(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--list") {
            options.list = true;
        } else if (arg == "--list-trackers") {
            options.listTrackers = true;
        } else if (arg == "--clear") {
            options.clear = true;
        } else if (arg == "--measure-only") {
            options.measureOnly = true;
        } else if (arg == "--tracker") {
            if (i + 1 >= argc) {
                return false;
            }
            options.trackerSerial = argv[++i];
        } else if (arg == "--reference") {
            if (i + 1 >= argc || !ParseReference(argv[++i], options.reference)) {
                return false;
            }
        } else if (arg == "--controller") {
            if (i + 1 >= argc) {
                return false;
            }
            const std::string role = argv[++i];
            if (role == "left") {
                options.reference = ReferenceKind::LeftController;
            } else if (role == "right") {
                options.reference = ReferenceKind::RightController;
            } else {
                return false;
            }
        } else if (arg == "--samples") {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], options.samples)) {
                return false;
            }
        } else if (arg == "--interval-ms") {
            if (i + 1 >= argc || !ParsePositiveInt(argv[++i], options.intervalMs)) {
                return false;
            }
        } else if (arg == "--help" || arg == "-h") {
            PrintUsage();
            std::exit(0);
        } else {
            return false;
        }
    }

    const int modeCount = (options.list ? 1 : 0) +
                          (options.listTrackers ? 1 : 0) +
                          (options.clear ? 1 : 0) +
                          (!options.trackerSerial.empty() ? 1 : 0);
    if (modeCount != 1) {
        return false;
    }
    if (!options.trackerSerial.empty() && options.reference == ReferenceKind::None) {
        return false;
    }
    if (options.measureOnly && options.trackerSerial.empty()) {
        return false;
    }
    return true;
}

bool OpenAlignmentMapping(AlignmentMapping& ipc, std::string& error) {
    ipc.mapping = OpenFileMappingW(
        FILE_MAP_ALL_ACCESS,
        FALSE,
        pico_ot::steamvr::kWorldAlignmentMappingName);
    if (ipc.mapping == nullptr) {
        error = "world-alignment mapping not found; start SteamVR with the updated driver first";
        return false;
    }

    ipc.state = static_cast<pico_ot::steamvr::WorldAlignmentSharedState*>(
        MapViewOfFile(
            ipc.mapping,
            FILE_MAP_ALL_ACCESS,
            0,
            0,
            sizeof(pico_ot::steamvr::WorldAlignmentSharedState)));
    if (ipc.state == nullptr) {
        error = "failed to map world-alignment shared state";
        return false;
    }

    ipc.updatedEvent = OpenEventW(
        EVENT_MODIFY_STATE,
        FALSE,
        pico_ot::steamvr::kWorldAlignmentUpdatedEventName);
    if (ipc.updatedEvent == nullptr) {
        error = "world-alignment update event not found";
        return false;
    }

    if (ipc.state->magic != pico_ot::steamvr::kWorldAlignmentMagic ||
        ipc.state->version != pico_ot::steamvr::kWorldAlignmentVersion) {
        error = "world-alignment shared-state ABI mismatch";
        return false;
    }
    return true;
}

bool WriteTranslation(AlignmentMapping& ipc,
                      const std::array<double, 3>& translation,
                      std::string& error) {
    ipc.state->translationMeters[0] = translation[0];
    ipc.state->translationMeters[1] = translation[1];
    ipc.state->translationMeters[2] = translation[2];
    ++ipc.state->generation;
    MemoryBarrier();

    if (!SetEvent(ipc.updatedEvent)) {
        error = "failed to signal world-alignment update event";
        return false;
    }
    return true;
}

std::string GetDeviceStringProperty(vr::IVRSystem* system,
                                    vr::TrackedDeviceIndex_t index,
                                    vr::ETrackedDeviceProperty property) {
    char buffer[256]{};
    vr::ETrackedPropertyError propertyError = vr::TrackedProp_Success;
    system->GetStringTrackedDeviceProperty(
        index,
        property,
        buffer,
        static_cast<std::uint32_t>(sizeof(buffer)),
        &propertyError);
    if (propertyError != vr::TrackedProp_Success) {
        return {};
    }
    return buffer;
}

std::string GetDeviceSerial(vr::IVRSystem* system, vr::TrackedDeviceIndex_t index) {
    return GetDeviceStringProperty(system, index, vr::Prop_SerialNumber_String);
}

bool IsPicoBridgeTracker(vr::IVRSystem* system, vr::TrackedDeviceIndex_t index) {
    if (system->GetTrackedDeviceClass(index) != vr::TrackedDeviceClass_GenericTracker) {
        return false;
    }
    return GetDeviceStringProperty(system, index, vr::Prop_TrackingSystemName_String) ==
           "pico_ot_bridge";
}

const char* DeviceClassName(vr::ETrackedDeviceClass deviceClass) {
    switch (deviceClass) {
    case vr::TrackedDeviceClass_HMD:
        return "HMD";
    case vr::TrackedDeviceClass_Controller:
        return "Controller";
    case vr::TrackedDeviceClass_GenericTracker:
        return "GenericTracker";
    case vr::TrackedDeviceClass_TrackingReference:
        return "TrackingReference";
    case vr::TrackedDeviceClass_DisplayRedirect:
        return "DisplayRedirect";
    default:
        return "Invalid";
    }
}

const char* RoleName(vr::ETrackedControllerRole role) {
    switch (role) {
    case vr::TrackedControllerRole_LeftHand:
        return "Left";
    case vr::TrackedControllerRole_RightHand:
        return "Right";
    default:
        return "-";
    }
}

const char* ReferenceName(ReferenceKind reference) {
    switch (reference) {
    case ReferenceKind::LeftController:
        return "Left Controller";
    case ReferenceKind::RightController:
        return "Right Controller";
    case ReferenceKind::Hmd:
        return "HMD";
    default:
        return "Invalid";
    }
}

std::array<double, 3> PoseTranslation(const vr::TrackedDevicePose_t& pose) {
    return {
        static_cast<double>(pose.mDeviceToAbsoluteTracking.m[0][3]),
        static_cast<double>(pose.mDeviceToAbsoluteTracking.m[1][3]),
        static_cast<double>(pose.mDeviceToAbsoluteTracking.m[2][3])};
}

void ListDevices(vr::IVRSystem* system) {
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding,
        0.0F,
        poses,
        vr::k_unMaxTrackedDeviceCount);

    std::cout << "SteamVR devices in TrackingUniverseStanding:\n";
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        const vr::ETrackedDeviceClass deviceClass = system->GetTrackedDeviceClass(i);
        if (deviceClass == vr::TrackedDeviceClass_Invalid) {
            continue;
        }

        const vr::ETrackedControllerRole role =
            system->GetControllerRoleForTrackedDeviceIndex(i);
        const std::string serial = GetDeviceSerial(system, i);
        std::cout << "  [" << i << "] " << DeviceClassName(deviceClass)
                  << " role=" << RoleName(role)
                  << " connected=" << (poses[i].bDeviceIsConnected ? 1 : 0)
                  << " valid=" << (poses[i].bPoseIsValid ? 1 : 0)
                  << " serial=" << serial;
        if (poses[i].bPoseIsValid) {
            const auto p = PoseTranslation(poses[i]);
            std::cout << std::fixed << std::setprecision(4)
                      << " p=(" << p[0] << ", " << p[1] << ", " << p[2] << ")";
        }
        std::cout << '\n';
    }
}

void ListPicoTrackers(vr::IVRSystem* system) {
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};
    system->GetDeviceToAbsoluteTrackingPose(
        vr::TrackingUniverseStanding,
        0.0F,
        poses,
        vr::k_unMaxTrackedDeviceCount);

    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (!IsPicoBridgeTracker(system, i) || !poses[i].bDeviceIsConnected) {
            continue;
        }
        const std::string serial = GetDeviceSerial(system, i);
        if (!serial.empty()) {
            std::cout << serial << '\n';
        }
    }
}

std::optional<vr::TrackedDeviceIndex_t> FindTracker(
    vr::IVRSystem* system,
    const std::string& serial) {
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (!IsPicoBridgeTracker(system, i)) {
            continue;
        }
        if (GetDeviceSerial(system, i) == serial) {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<vr::TrackedDeviceIndex_t> FindReference(
    vr::IVRSystem* system,
    ReferenceKind reference) {
    if (reference == ReferenceKind::Hmd) {
        for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
            if (system->GetTrackedDeviceClass(i) == vr::TrackedDeviceClass_HMD) {
                return i;
            }
        }
        return std::nullopt;
    }

    const vr::ETrackedControllerRole wantedRole =
        reference == ReferenceKind::LeftController
            ? vr::TrackedControllerRole_LeftHand
            : vr::TrackedControllerRole_RightHand;
    for (vr::TrackedDeviceIndex_t i = 0; i < vr::k_unMaxTrackedDeviceCount; ++i) {
        if (system->GetTrackedDeviceClass(i) != vr::TrackedDeviceClass_Controller) {
            continue;
        }
        if (system->GetControllerRoleForTrackedDeviceIndex(i) == wantedRole) {
            return i;
        }
    }
    return std::nullopt;
}

int RunCalibration(vr::IVRSystem* system,
                   AlignmentMapping& ipc,
                   const Options& options) {
    const auto trackerIndex = FindTracker(system, options.trackerSerial);
    if (!trackerIndex.has_value()) {
        std::cerr << "PICO bridge tracker serial was not found in SteamVR: "
                  << options.trackerSerial << '\n';
        return 4;
    }

    const auto referenceIndex = FindReference(system, options.reference);
    if (!referenceIndex.has_value()) {
        std::cerr << "Requested reference device was not found in SteamVR: "
                  << ReferenceName(options.reference) << '\n';
        return 5;
    }

    std::array<double, 3> deltaSum{0.0, 0.0, 0.0};
    int validSamples = 0;
    vr::TrackedDevicePose_t poses[vr::k_unMaxTrackedDeviceCount]{};

    std::cout << "Tracker:   index=" << *trackerIndex
              << " serial=" << options.trackerSerial << '\n';
    std::cout << "Reference: index=" << *referenceIndex
              << " type=" << ReferenceName(options.reference) << '\n';
    std::cout << "Sampling " << options.samples
              << " frames; keep both reference points together and still.\n";

    for (int sample = 0; sample < options.samples; ++sample) {
        system->GetDeviceToAbsoluteTrackingPose(
            vr::TrackingUniverseStanding,
            0.0F,
            poses,
            vr::k_unMaxTrackedDeviceCount);

        const auto& trackerPose = poses[*trackerIndex];
        const auto& referencePose = poses[*referenceIndex];
        if (trackerPose.bDeviceIsConnected && trackerPose.bPoseIsValid &&
            referencePose.bDeviceIsConnected && referencePose.bPoseIsValid) {
            const auto trackerPosition = PoseTranslation(trackerPose);
            const auto referencePosition = PoseTranslation(referencePose);
            for (std::size_t axis = 0; axis < deltaSum.size(); ++axis) {
                deltaSum[axis] += referencePosition[axis] - trackerPosition[axis];
            }
            ++validSamples;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(options.intervalMs));
    }

    const int minimumValidSamples = std::max(10, options.samples / 4);
    if (validSamples < minimumValidSamples) {
        std::cerr << "Too few valid paired poses: " << validSamples
                  << "/" << options.samples << '\n';
        return 6;
    }

    std::array<double, 3> meanDelta{};
    std::array<double, 3> currentTranslation{
        ipc.state->translationMeters[0],
        ipc.state->translationMeters[1],
        ipc.state->translationMeters[2]};
    std::array<double, 3> updatedTranslation{};
    for (std::size_t axis = 0; axis < meanDelta.size(); ++axis) {
        meanDelta[axis] = deltaSum[axis] / static_cast<double>(validSamples);
        updatedTranslation[axis] = currentTranslation[axis] + meanDelta[axis];
    }

    std::cout << std::fixed << std::setprecision(6)
              << "Valid samples: " << validSamples << '\n'
              << "Current t: (" << currentTranslation[0] << ", "
              << currentTranslation[1] << ", " << currentTranslation[2] << ") m\n"
              << "Measured delta: (" << meanDelta[0] << ", "
              << meanDelta[1] << ", " << meanDelta[2] << ") m\n"
              << "Updated t: (" << updatedTranslation[0] << ", "
              << updatedTranslation[1] << ", " << updatedTranslation[2] << ") m\n"
              << "CALIBRATION_RESULT " << updatedTranslation[0] << ' '
              << updatedTranslation[1] << ' ' << updatedTranslation[2] << '\n';

    if (options.measureOnly) {
        std::cout << "Measurement only; world translation was not changed.\n";
        return 0;
    }

    std::string error;
    if (!WriteTranslation(ipc, updatedTranslation, error)) {
        std::cerr << error << '\n';
        return 7;
    }

    std::cout << "World translation update requested.\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    if (!ParseArgs(argc, argv, options)) {
        PrintUsage();
        return 2;
    }

    AlignmentMapping ipc;
    std::string ipcError;
    if (!OpenAlignmentMapping(ipc, ipcError)) {
        std::cerr << ipcError << '\n';
        return 3;
    }

    if (options.clear) {
        const std::array<double, 3> zero{0.0, 0.0, 0.0};
        std::string error;
        if (!WriteTranslation(ipc, zero, error)) {
            std::cerr << error << '\n';
            return 7;
        }
        std::cout << "World translation cleared.\n";
        return 0;
    }

    vr::EVRInitError initError = vr::VRInitError_None;
    vr::IVRSystem* system = vr::VR_Init(&initError, vr::VRApplication_Utility);
    if (system == nullptr || initError != vr::VRInitError_None) {
        std::cerr << "VR_Init failed: "
                  << vr::VR_GetVRInitErrorAsEnglishDescription(initError) << '\n';
        return 8;
    }

    int result = 0;
    if (options.list) {
        ListDevices(system);
    } else if (options.listTrackers) {
        ListPicoTrackers(system);
    } else {
        result = RunCalibration(system, ipc, options);
    }

    vr::VR_Shutdown();
    return result;
}
