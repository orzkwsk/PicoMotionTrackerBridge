#include "pico_ot_backend/reacquire_admission.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pico_ot {
namespace {
std::optional<double> Finite(double v) { return std::isfinite(v) ? std::optional<double>(v) : std::nullopt; }
double Norm(const bridge::Quatd& q) { return std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w)); }
}
void AdmissionConfig::Validate() const {
    if (reacquire_confirm_ms < 0 || reacquire_confirm_ms > (std::numeric_limits<int64_t>::max)()/1'000'000 ||
        reacquire_min_samples < 0) throw std::invalid_argument("invalid PICO admission confirmation values");
}
const char* AdmissionStateName(AdmissionState s) noexcept {
    switch(s) { case AdmissionState::Invalid: return "INVALID";
        case AdmissionState::Candidate: return "CANDIDATE";
        case AdmissionState::Admitted: return "ADMITTED"; }
    return "INVALID";
}
PicoReacquireAdmission::PicoReacquireAdmission(AdmissionConfig config, int64_t gap)
    : config_(config), maxGapNs_(gap) {
    config_.Validate();
    if (gap <= 0) throw std::invalid_argument("invalid admission freshness gap");
}
AdmissionResult PicoReacquireAdmission::Process(const bridge::PosePacket& raw, int64_t t, bool valid, bool fresh) noexcept {
    AdmissionResult r; r.raw_valid=valid;
    const auto previous=state_;
    const bool progressing=!lastTime_ || t>*lastTime_;
    const bool regressed=t<0 || (lastTime_ && t<*lastTime_);
    const bool gap=lastTime_ && progressing && *lastTime_>=0 && t-*lastTime_>maxGapNs_;
    if (lastTime_) {
        if (t>=0 && *lastTime_>=0) r.sample_dt_ms=static_cast<double>(t-*lastTime_)/1e6;
        r.position_step_m=Finite(std::hypot(std::hypot(raw.positionMeters.x-lastPosition_.x,
            raw.positionMeters.y-lastPosition_.y),raw.positionMeters.z-lastPosition_.z));
        const double a=Norm(raw.orientationXyzw), b=Norm(lastQuaternion_);
        if (std::isfinite(a) && std::isfinite(b) && a>0 && b>0) {
            const auto& q=raw.orientationXyzw; const auto& p=lastQuaternion_;
            const double dot=(q.x/a)*(p.x/b)+(q.y/a)*(p.y/b)+(q.z/a)*(p.z/b)+(q.w/a)*(p.w/b);
            r.quaternion_step_rad=Finite(2*std::acos(std::clamp(std::abs(dot),0.0,1.0)));
        }
        if (r.sample_dt_ms && *r.sample_dt_ms>0) {
            if (r.position_step_m) r.linear_speed_mps=Finite(*r.position_step_m/(*r.sample_dt_ms/1000));
            if (r.quaternion_step_rad) r.angular_speed_radps=Finite(*r.quaternion_step_rad/(*r.sample_dt_ms/1000));
        }
    }
    if (progressing && !regressed) { lastTime_=t; lastPosition_=raw.positionMeters; lastQuaternion_=raw.orientationXyzw; }
    if (!config_.enabled) {
        state_=valid && fresh ? AdmissionState::Admitted : AdmissionState::Invalid;
        start_.reset(); samples_=0; r.reason="gate_disabled_compatibility";
    } else if (!valid || !fresh || regressed) {
        state_=AdmissionState::Invalid; start_.reset(); samples_=0;
        r.reason=regressed ? "timestamp_regressed" : !fresh ? "stale" : "raw_invalid";
    } else {
        if (gap) { state_=AdmissionState::Invalid; start_.reset(); samples_=0; }
        // A duplicate can invalidate immediately, but cannot begin/advance a candidate.
        if (state_==AdmissionState::Invalid && progressing) {
            state_=AdmissionState::Candidate; start_=t; samples_=1;
            r.reason=gap ? "timestamp_gap_restart" : "raw_valid_candidate";
        } else if (progressing && state_==AdmissionState::Candidate) {
            if (samples_<(std::numeric_limits<int64_t>::max)()) ++samples_;
            r.reason="confirmation_pending";
        } else r.reason=progressing ? "admitted" : "duplicate_timestamp";
        if (start_) r.candidate_elapsed_ns=(lastTime_ ? *lastTime_ : t)-*start_;
        if (state_==AdmissionState::Candidate && progressing &&
            r.candidate_elapsed_ns>=config_.reacquire_confirm_ms*1'000'000 &&
            samples_>=config_.reacquire_min_samples) {
            state_=AdmissionState::Admitted; r.reason="duration_and_samples_confirmed";
        }
    }
    r.state=state_; r.effective_valid=state_==AdmissionState::Admitted;
    r.transitioned=state_!=previous; r.candidate_start_ns=start_; r.candidate_samples=samples_;
    return r;
}
}
