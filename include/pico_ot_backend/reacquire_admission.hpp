#pragma once
#include "pico_ot_bridge/protocol.hpp"
#include <optional>

namespace pico_ot {
// All confirmation values are PROVISIONAL / DISABLED-BY-DEFAULT.
struct AdmissionConfig {
    bool enabled = false;
    int64_t reacquire_confirm_ms = 0;
    int64_t reacquire_min_samples = 0;
    void Validate() const;
};
enum class AdmissionState { Invalid, Candidate, Admitted };
const char* AdmissionStateName(AdmissionState state) noexcept;
struct AdmissionResult {
    AdmissionState state = AdmissionState::Invalid;
    bool raw_valid = false, effective_valid = false, transitioned = false;
    std::optional<int64_t> candidate_start_ns;
    int64_t candidate_elapsed_ns = 0, candidate_samples = 0;
    std::optional<double> sample_dt_ms, position_step_m, quaternion_step_rad;
    std::optional<double> linear_speed_mps, angular_speed_radps;
    const char* reason = "raw_invalid";
};
// Fixed-size, allocation-free state. Own one per device; independent lifecycle
// events discard it. No identity, protocol session, space or Core state mutator.
class PicoReacquireAdmission {
public:
    explicit PicoReacquireAdmission(AdmissionConfig config = {}, int64_t maxGapNs = 500'000'000);
    AdmissionResult Process(const bridge::PosePacket& raw, int64_t monotonicNs,
                            bool rawValid, bool fresh = true) noexcept;
    bool Enabled() const noexcept { return config_.enabled; }
    const AdmissionConfig& Config() const noexcept { return config_; }
private:
    AdmissionConfig config_;
    int64_t maxGapNs_;
    AdmissionState state_ = AdmissionState::Invalid;
    std::optional<int64_t> start_, lastTime_;
    int64_t samples_ = 0;
    bridge::Vec3d lastPosition_;
    bridge::Quatd lastQuaternion_;
};
}
