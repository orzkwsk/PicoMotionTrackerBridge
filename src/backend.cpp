#include "pico_ot_backend/backend.hpp"

#include "internal/clock_mapper.hpp"

#include <algorithm>
#include <utility>

namespace pico_ot {

class Backend::ClockMapper {
public:
    ClockMapper(size_t windowSamples, int64_t thresholdNs)
        : mapper(windowSamples, thresholdNs) {}
    internal::ClockMapper mapper;
};

Backend::Backend(std::unique_ptr<IPicoRuntimeAdapter> runtime)
    : runtime_(std::move(runtime)) {}

Backend::~Backend() { Stop(); }

Status Backend::Start(const BackendConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!runtime_) return {StatusCode::RuntimeNotReady, 0, "runtime adapter is null"};

    if (started_) runtime_->Stop();
    devices_.clear();
    config_ = config;
    clockMapper_ = std::make_unique<ClockMapper>(
        config.clockWindowSamples,
        config.clockDiscontinuityThresholdNs);

    Status status = runtime_->Start(config.runtime);
    if (!status.ok()) {
        clockMapper_.reset();
        return status;
    }
    started_ = true;

    std::vector<DeviceInfo> devices;
    status = runtime_->EnumerateTrackers(devices);
    if (!status.ok()) {
        runtime_->Stop();
        started_ = false;
        clockMapper_.reset();
        return status;
    }
    MergeDevices(devices);
    return Status::Ok();
}

void Backend::Stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (runtime_) runtime_->Stop();
    started_ = false;
    devices_.clear();
    clockMapper_.reset();
}

Status Backend::RefreshDevices() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) return {StatusCode::RuntimeNotReady, 0, "backend not started"};
    std::vector<DeviceInfo> devices;
    Status status = runtime_->EnumerateTrackers(devices);
    if (!status.ok()) return status;
    MergeDevices(devices);
    return Status::Ok();
}

Status Backend::ReinitializeRuntime() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) return {StatusCode::RuntimeNotReady, 0, "backend not started"};
    Status status = runtime_->ReinitializeTrackerMap();
    if (status.ok() && clockMapper_) clockMapper_->mapper.Reset();
    return status;
}

std::vector<DeviceInfo> Backend::Devices() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DeviceInfo> out;
    out.reserve(devices_.size());
    for (const auto& [_, device] : devices_) out.push_back(device);
    std::sort(out.begin(), out.end(), [](const DeviceInfo& a, const DeviceInfo& b) {
        return a.serial < b.serial;
    });
    return out;
}

RuntimeDiagnostics Backend::Diagnostics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return runtime_ ? runtime_->Diagnostics() : RuntimeDiagnostics{};
}

void Backend::MergeDevices(const std::vector<DeviceInfo>& devices) {
    for (auto& [_, cached] : devices_) cached.connected = false;
    for (const auto& device : devices) devices_[device.serial] = device;
}

DeviceInfo Backend::LookupDeviceSnapshot(const std::string& serial) const {
    const auto it = devices_.find(serial);
    if (it == devices_.end()) return {};
    return it->second;
}

Status Backend::PollRaw(const std::string& serial, RawTrackerState& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!started_) return {StatusCode::RuntimeNotReady, 0, "backend not started"};
    if (serial.empty()) return {StatusCode::InvalidArgument, 0, "serial is empty"};

    Status status = runtime_->ReadTrackerPose(serial, out);
    if (!status.ok()) return status;

    const DeviceInfo device = LookupDeviceSnapshot(serial);
    out.runtimeDeviceId = device.runtimeDeviceId;
    out.connectedSnapshot = device.connected;

    if (clockMapper_) {
        int64_t mapped = 0;
        if (clockMapper_->mapper.Observe(out.sourceTimestamp, out.observedMonotonicNs, mapped)) {
            out.mappedMonotonicNs = mapped;
            out.mappedMonotonicValid = true;
        }
    }
    return Status::Ok();
}

TrackerState Backend::Normalize(const RawTrackerState& raw) const {
    constexpr double kMmToM = 0.001;
    TrackerState out{};
    out.serial = raw.serial;
    out.runtimeDeviceId = raw.runtimeDeviceId;
    out.connectedSnapshot = raw.connectedSnapshot;
    out.sourceTimestamp = raw.sourceTimestamp;
    out.observedMonotonicNs = raw.observedMonotonicNs;
    out.mappedMonotonicNs = raw.mappedMonotonicNs;
    out.mappedMonotonicValid = raw.mappedMonotonicValid;
    out.positionValid = raw.positionValid;
    out.orientationSamplePresent = raw.orientationSamplePresent;
    out.rawTrackingState65 = raw.rawTrackingState65;
    out.vendorReturnCode = raw.vendorReturnCode;
    out.positionMeters = {raw.positionMm.x * kMmToM, raw.positionMm.y * kMmToM, raw.positionMm.z * kMmToM};
    out.orientationXyzw = raw.orientationXyzw;
    out.linearVelocityMetersPerSec = {
        raw.linearVelocityMmPerSec.x * kMmToM,
        raw.linearVelocityMmPerSec.y * kMmToM,
        raw.linearVelocityMmPerSec.z * kMmToM};
    out.linearAccelerationMetersPerSec2 = Vec3d{
        raw.linearAccelerationRaw.x * kMmToM,
        raw.linearAccelerationRaw.y * kMmToM,
        raw.linearAccelerationRaw.z * kMmToM};
    out.angularVelocityRadPerSec = raw.angularVelocityRawRadPerSec;
    out.frame = raw.frame;
    out.angularVelocityFrame = raw.angularVelocityFrame;
    return out;
}

Status Backend::Poll(const std::string& serial, TrackerState& out) {
    RawTrackerState raw{};
    Status status = PollRaw(serial, raw);
    if (!status.ok()) return status;
    out = Normalize(raw);
    return Status::Ok();
}

Status Backend::PollAll(std::vector<TrackerState>& out) {
    const std::vector<DeviceInfo> devices = Devices();
    out.clear();
    out.reserve(devices.size());
    for (const auto& device : devices) {
        if (!device.connected) continue;
        TrackerState state{};
        Status status = Poll(device.serial, state);
        if (!status.ok()) return status;
        out.push_back(std::move(state));
    }
    return Status::Ok();
}

}  // namespace pico_ot
