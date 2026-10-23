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
PicoObservationAdapter::PicoObservationAdapter(ObservationConfig config) : config_(std::move(config)) {
    if (!config_.newUuid || !config_.sourceSpace || config_.clockEpochPcNs < 0 ||
        config_.staleTimeoutNs <= 0 || config_.maxDevices == 0) throw std::invalid_argument("invalid Observation configuration");
    session_ = config_.newUuid(); clock_ = config_.newUuid();
    p::ObservationDeviceState probe;
    Header(probe,"configuration-probe",0,config_.clockEpochPcNs,config_.clockEpochPcNs);
    probe.presence = "unknown"; probe.tracking_state = "unknown";
    std::string wire; p::Error error;
    if (!p::EncodeEnvelope(probe,wire,error)) throw std::invalid_argument(error.message);
}
void PicoObservationAdapter::Rotate() {
    const auto session = config_.newUuid();
    session_ = session; progress_.clear();
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
    const auto space=config_.sourceSpace(sender);
    if(space.revision<config_.spaceRevision)throw std::runtime_error("source-space revision rollback");
    if(space.revision!=config_.spaceRevision||space.known!=spaceKnown_){
        config_.spaceRevision=space.revision;spaceKnown_=space.known;
        // Reset sequence numbers only with a new input session. Otherwise a
        // known/unknown transition can be rejected as stale by the consumer.
        if(!progress_.empty())Rotate();
    }
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
        auto& seen = progress_[serial];
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
                if (Finite(pose.positionMeters)) v.position = Vec(pose.positionMeters);
                const auto& q = pose.orientationXyzw;
                if (std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w)) v.orientation = p::QuatXyzw{q.x,q.y,q.z,q.w};
                v.validity.position = fresh && pose.positionValid && v.position.has_value();
                v.validity.orientation = fresh && pose.orientationSamplePresent && OrientationSane(q);
                // Sample sanity alone does not establish PICO rotation-only tracking
                // during optical loss. Preserve diagnostics, but fail closed.
                const bool full = spaceKnown_ && v.validity.position && v.validity.orientation;
                v.validity = {full, full};
                v.modality = full ? "full" : "none";
                v.orientation_evidence = v.validity.orientation ? "sample_sanity" : "none";
                v.tracking_state = v.validity.position && v.validity.orientation ? "tracked" :
                    v.validity.position || v.validity.orientation ? "degraded" : "lost";
                if (v.validity.position && Finite(pose.linearVelocityMetersPerSec))
                    v.linear_velocity = p::Derivative{Vec(pose.linearVelocityMetersPerSec),"space","measured"};
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
