#pragma once
#include "monaka/protocol/v1/codec.hpp"
#include "pico_ot_bridge/receiver.hpp"
#include <functional>
#include <map>

namespace pico_ot {
struct ObservationConfig {
    std::string sourceId;
    std::string spaceId;
    uint32_t spaceRevision = 0;
    int64_t clockEpochPcNs = 0;
    int64_t staleTimeoutNs = 500'000'000;
    size_t maxDevices = 256;
    std::function<std::string()> newUuid;
    // Must persist the revision before returning it; failure disables the new session.
    std::function<uint32_t()> nextSpaceRevision;
};

// PC-only conversion, always using immutable Receiver admission times, never
// ToTrackerState()/posePcMonotonicNs. Collect is called by the existing owner.
class PicoObservationAdapter {
public:
    explicit PicoObservationAdapter(ObservationConfig config);
    std::vector<monaka::protocol::v1::Envelope> Collect(
        const bridge::Receiver& receiver, int64_t nowPcNs);
    bool StampSentAt(monaka::protocol::v1::Envelope& value, int64_t encodingPcNs) const;
    [[nodiscard]] uint64_t RejectedSamples() const noexcept { return rejected_; }
    [[nodiscard]] const std::string& SessionId() const noexcept { return session_; }
private:
    struct Progress {
        bool poseSeen = false, deviceSeen = false;
        uint32_t pose = 0, device = 0;
        int64_t nextPose = 0, nextDevice = 0;
    };
    ObservationConfig config_;
    std::string session_, clock_;
    uint64_t sender_ = 0, rejected_ = 0;
    std::map<std::string, Progress> progress_;
    void Rotate();
    template<class T> void Header(T& value, const std::string& serial,
                                 int64_t sequence, int64_t received, int64_t now) const;
};
} // namespace pico_ot
