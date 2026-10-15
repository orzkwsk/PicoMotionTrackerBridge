#pragma once
#include "pico_ot_backend/observation_adapter.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <limits>

namespace pico_ot {
inline AdmissionConfig ParseAdmissionConfig(const nlohmann::json& j) {
    if (!j.is_object()) throw std::invalid_argument("admission config must be an object");
    AdmissionConfig c;
    for (const auto& item:j.items()) {
        if (item.key()=="enabled") {
            if (!item.value().is_boolean()) throw std::invalid_argument("enabled must be boolean");
            c.enabled=item.value().get<bool>();
        } else if (item.key()=="reacquire_confirm_ms" || item.key()=="reacquire_min_samples") {
            if (!item.value().is_number_integer() || item.value()<0 ||
                item.value()>nlohmann::json((std::numeric_limits<int64_t>::max)()))
                throw std::invalid_argument("confirmation must be a nonnegative int64");
            const auto v=item.value().get<int64_t>();
            if (item.key()=="reacquire_confirm_ms") c.reacquire_confirm_ms=v;
            else c.reacquire_min_samples=v;
        } else throw std::invalid_argument("unknown admission config key: "+item.key());
    }
    c.Validate(); return c;
}
inline nlohmann::json AdmissionConfigJson(const AdmissionConfig& c) {
    return {{"enabled",c.enabled},{"reacquire_confirm_ms",c.reacquire_confirm_ms},
        {"reacquire_min_samples",c.reacquire_min_samples},{"threshold_policy","PROVISIONAL / DISABLED-BY-DEFAULT"}};
}
inline nlohmann::json AdmissionDiagnosticJson(const bridge::PosePacket& raw,
    const AdmissionResult& r, const monaka::protocol::v2::TrackerObservation& v) {
    using J=nlohmann::json;
    auto number=[](const std::optional<double>& x)->J { return x ? J(*x) : J(nullptr); };
    return {{"raw_valid",r.raw_valid},{"effective_valid",r.effective_valid},
        {"raw_position_valid",raw.positionValid},{"raw_orientation_present",raw.orientationSamplePresent},
        {"admission_state",AdmissionStateName(r.state)},{"transition",r.transitioned},{"reason",r.reason},
        {"candidate_start_ns",r.candidate_start_ns ? J(*r.candidate_start_ns) : J(nullptr)},
        {"candidate_elapsed_ms",static_cast<double>(r.candidate_elapsed_ns)/1e6},{"candidate_samples",r.candidate_samples},
        {"raw_position",{raw.positionMeters.x,raw.positionMeters.y,raw.positionMeters.z}},
        {"raw_quaternion",{raw.orientationXyzw.x,raw.orientationXyzw.y,raw.orientationXyzw.z,raw.orientationXyzw.w}},
        {"raw_linear_velocity",{raw.linearVelocityMetersPerSec.x,raw.linearVelocityMetersPerSec.y,raw.linearVelocityMetersPerSec.z}},
        {"raw_angular_velocity",{raw.angularVelocityRadPerSec.x,raw.angularVelocityRadPerSec.y,raw.angularVelocityRadPerSec.z}},
        {"position_step_m",number(r.position_step_m)},{"quaternion_step_rad",number(r.quaternion_step_rad)},
        {"linear_speed_mps",number(r.linear_speed_mps)},{"angular_speed_radps",number(r.angular_speed_radps)},
        {"sample_dt_ms",number(r.sample_dt_ms)},{"native_tracking_state",raw.rawTrackingState65},
        {"source_timestamp",std::to_string(raw.sourceTimestamp)},{"source_id",v.source_id},{"device_id",v.device_id},
        {"session_id",v.session_id},{"clock_id",v.clock_id},{"sequence",std::to_string(v.sequence)},
        {"timestamp_ns",std::to_string(v.timestamp_ns)},
        {"coordinate_space",{{"id",v.coordinate_space.id},{"convention",v.coordinate_space.convention},{"revision",v.coordinate_space.revision}}},
        {"capabilities",v.capabilities}};
}
}
