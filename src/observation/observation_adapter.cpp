#include "pico_ot_backend/observation_adapter.hpp"
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace pico_ot {
namespace p = monaka::protocol::v2;
namespace {
bool Finite(const bridge::Vec3d& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
p::Vec3 Vec(const bridge::Vec3d& v) { return {v.x,v.y,v.z}; }
bool OrientationSane(const bridge::Quatd& q) {
    if (!std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z) || !std::isfinite(q.w)) return false;
    const double norm = std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w));
    return norm >= 0.5 && norm <= 1.5;
}
std::optional<p::Battery> Battery(const bridge::ReceivedTrackerState& state, int64_t epoch, int64_t now) {
    if (!state.deviceState || state.lastDeviceReceivePcNs < epoch || state.lastDeviceReceivePcNs > now) return std::nullopt;
    p::Battery battery;
    battery.timestamp_ns = state.lastDeviceReceivePcNs - epoch;
    // POTB contains an enumeration snapshot, not a live charging-valid flag.
    // Raw bucket and cached charging must not become a percentage/live claim.
    return battery;
}
}
AdmissionResult ApplyPicoAdmission(p::TrackerObservation& v, const bridge::PosePacket& pose,
    int64_t time, bool fresh, PicoReacquireAdmission& gate) {
    if (Finite(pose.positionMeters)) v.position=Vec(pose.positionMeters);
    const auto& q=pose.orientationXyzw;
    if (std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w))
        v.orientation=p::QuatXyzw{q.x,q.y,q.z,q.w};
    const bool rawFull=pose.positionValid && pose.orientationSamplePresent &&
        v.position.has_value() && OrientationSane(q);
    const auto result=gate.Process(pose,time,rawFull,fresh);
    const bool full=result.effective_valid;
    v.validity={full,full}; v.modality=full ? "full" : "none";
    v.orientation_evidence=full ? "sample_sanity" : "none";
    v.tracking_state=full ? "tracked" : "lost";
    v.linear_velocity.reset(); v.angular_velocity.reset(); v.linear_acceleration.reset();
    if (full && Finite(pose.linearVelocityMetersPerSec))
        v.linear_velocity=p::Derivative{Vec(pose.linearVelocityMetersPerSec),"space","measured"};
    // Enabled-gate raw pose is capture-only until admitted. Disabled mode keeps
    // the exact historical diagnostic numeric fields and validity semantics.
    if (gate.Enabled() && !full) { v.position.reset(); v.orientation.reset(); }
    return result;
}
PicoObservationAdapter::PicoObservationAdapter(ObservationConfig config) : config_(std::move(config)) {
    config_.admission.Validate();
    if (!config_.newUuid || !config_.nextSpaceRevision || config_.clockEpochPcNs < 0 ||
        config_.staleTimeoutNs <= 0 || config_.maxDevices == 0) throw std::invalid_argument("invalid Observation configuration");
    session_ = config_.newUuid(); clock_ = config_.newUuid();
    p::ObservationDeviceState probe;
    Header(probe,"configuration-probe",0,config_.clockEpochPcNs,config_.clockEpochPcNs);
    probe.presence = "unknown"; probe.tracking_state = "unknown";
    std::string wire; p::Error error;
    if (!p::EncodeEnvelope(probe,wire,error)) throw std::invalid_argument(error.message);
}
void PicoObservationAdapter::Rotate() {
    const auto revision = config_.nextSpaceRevision();
    if (revision <= config_.spaceRevision) throw std::runtime_error("space revision must increase; reconfigure before overflow");
    const auto session = config_.newUuid();
    config_.spaceRevision = revision; session_ = session; progress_.clear();
}
bool PicoObservationAdapter::StampSentAt(p::Envelope& value, int64_t now) const {
    if (now < config_.clockEpochPcNs) return false;
    return std::visit([&](auto& message) {
        const auto sentAt = now-config_.clockEpochPcNs;
        if (sentAt < message.timestamp_ns) return false;
        message.sent_at_ns = sentAt;
        return true;
    },value);
}
template<class T> void PicoObservationAdapter::Header(T& v, const std::string& serial,
    int64_t sequence, int64_t received, int64_t now) const {
    v.version = {2,0}; v.modality = "none"; v.source_id = config_.sourceId; v.session_id = session_; v.clock_id = clock_;
    v.sequence = sequence; v.timestamp_ns = received - config_.clockEpochPcNs;
    v.sent_at_ns = now - config_.clockEpochPcNs; v.timestamp_kind = "receive";
    v.device_id = serial;
    v.coordinate_space = {config_.spaceId,"pico-output-a-v1",config_.spaceRevision};
    v.capabilities = {"position","orientation","linear_velocity"};
}
std::vector<p::Envelope> PicoObservationAdapter::Collect(const bridge::Receiver& receiver, int64_t now) {
    std::vector<p::Envelope> output;
    if (now < config_.clockEpochPcNs) { ++rejected_; return output; }
    const auto sender = receiver.ActiveSenderSessionId();
    if (!sender) return output;
    if (sender != sender_) {
        if (sender_) Rotate();
        sender_ = sender;
    }
    for (const auto& entry : progress_) {
        if (entry.second.nextPose >= (std::numeric_limits<int64_t>::max)()-1 ||
            entry.second.nextDevice >= (std::numeric_limits<int64_t>::max)()-1) { Rotate(); break; }
    }
    for (const auto& serial : receiver.Serials()) {
        const auto* state = receiver.Find(serial);
        if (!state || state->senderSessionId != sender_) continue;
        if (!progress_.count(serial) && progress_.size() >= config_.maxDevices) { ++rejected_; continue; }
        auto& seen = progress_.try_emplace(serial,config_.admission,config_.staleTimeoutNs).first->second;
        auto admit = [&](p::Envelope value) {
            std::string wire; p::Error error;
            if (p::EncodeEnvelope(value,wire,error)) output.push_back(std::move(value));
            else ++rejected_;
        };
        if (state->hasPoseSequence && (!seen.poseSeen || seen.pose != state->lastPoseSequence)) {
            seen.poseSeen = true; seen.pose = state->lastPoseSequence;
            const auto received = state->lastPoseReceivePcNs;
            if (received < config_.clockEpochPcNs || received > now || state->pose.referenceFrame != 0) { ++rejected_; }
            else {
                p::TrackerObservation v;
                Header(v,serial,seen.nextPose++,received,now);
                const auto& pose = state->pose;
                const bool fresh = now-received <= config_.staleTimeoutNs;
                const auto admission=ApplyPicoAdmission(v,pose,received,fresh,seen.admission);
                if (config_.admissionDiagnostic) {
                    try { config_.admissionDiagnostic(pose,admission,v); }
                    catch (...) { ++diagnosticErrors_; }
                }
                // Angular frame and acceleration/gravity semantics are unresolved.
                v.battery = Battery(*state,config_.clockEpochPcNs,now);
                admit(std::move(v));
            }
        }
        if (state->hasDeviceSequence && state->deviceState && (!seen.deviceSeen || seen.device != state->lastDeviceSequence)) {
            seen.deviceSeen = true; seen.device = state->lastDeviceSequence;
            const auto received = state->lastDeviceReceivePcNs;
            if (received < config_.clockEpochPcNs || received > now) { ++rejected_; }
            else {
                p::ObservationDeviceState v;
                Header(v,serial,seen.nextDevice++,received,now);
                v.presence = "unknown"; v.tracking_state = "unknown";
                v.battery = Battery(*state,config_.clockEpochPcNs,now);
                admit(std::move(v));
            }
        }
    }
    return output;
}
} // namespace pico_ot
