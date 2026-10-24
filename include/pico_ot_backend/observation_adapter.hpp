#pragma once
#include "monaka/protocol/v2/codec.hpp"
#include "pico_ot_bridge/receiver.hpp"
#include "pico_ot_backend/reacquire_admission.hpp"
#include "observation_identity.hpp"
#include <functional>
#include <map>

namespace pico_ot {
// Shared by production adapter and offline replay. Header/identity is caller-owned.
AdmissionResult ApplyPicoAdmission(monaka::protocol::v2::TrackerObservation& observation,
    const bridge::PosePacket& raw, int64_t monotonicNs, bool fresh,
    PicoReacquireAdmission& gate, bool sourceSpaceKnown = true);
struct ObservationConfig {
    std::string sourceId;
    std::string spaceId;
    uint32_t spaceRevision = 0;
    int64_t clockEpochPcNs = 0;
    int64_t staleTimeoutNs = 500'000'000;
    size_t maxDevices = 256;
    std::function<std::string()> newUuid;
    // Called before publication; persistence failure prevents output.
    std::function<SourceSpaceState(uint64_t)> sourceSpace;
    AdmissionConfig admission;
    // Capture raw pose separately from the effective protocol envelope. Must be
    // bounded/nonblocking; failures are counted and never bypass the gate.
    std::function<void(const bridge::PosePacket&, const AdmissionResult&,
        const monaka::protocol::v2::TrackerObservation&)> admissionDiagnostic;
};

// PC-only conversion, always using immutable Receiver admission times, never
// ToTrackerState()/posePcMonotonicNs. Collect is called by the existing owner.
class PicoObservationAdapter {
public:
    explicit PicoObservationAdapter(ObservationConfig config);
    std::vector<monaka::protocol::v2::Envelope> Collect(
        const bridge::Receiver& receiver, int64_t nowPcNs);
    bool StampSentAt(monaka::protocol::v2::Envelope& value, int64_t encodingPcNs) const;
    [[nodiscard]] uint64_t RejectedSamples() const noexcept { return rejected_; }
    [[nodiscard]] const std::string& SessionId() const noexcept { return session_; }
    [[nodiscard]] uint64_t DiagnosticErrors() const noexcept { return diagnosticErrors_; }
private:
    struct Progress {
        explicit Progress(AdmissionConfig config = {}, int64_t gap = 500'000'000) : admission(config,gap) {}
        PicoReacquireAdmission admission;
        bool poseSeen = false, deviceSeen = false;
        uint32_t pose = 0, device = 0;
        int64_t nextPose = 0, nextDevice = 0;
    };
    ObservationConfig config_;
    std::string session_, clock_;
    uint64_t sender_ = 0, rejected_ = 0;
    uint64_t diagnosticErrors_ = 0;
    bool spaceKnown_=false;
    std::map<std::string, Progress> progress_;
    void Rotate();
    template<class T> void Header(T& value, const std::string& serial,
                                 int64_t sequence, int64_t received, int64_t now) const;
};
} // namespace pico_ot
