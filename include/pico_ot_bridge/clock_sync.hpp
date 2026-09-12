#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

namespace pico_ot::bridge {

struct ClockSyncObservation {
    int64_t clientSendMonotonicNs = 0;    // t0, PC
    int64_t serverReceiveMonotonicNs = 0; // t1, HMD
    int64_t serverSendMonotonicNs = 0;    // t2, HMD
    int64_t clientReceiveMonotonicNs = 0; // t3, PC
};

struct ClockSyncEstimate {
    // HMD monotonic ~= PC monotonic + hmdMinusPcNs.
    int64_t hmdMinusPcNs = 0;
    int64_t networkRoundTripNs = 0;
};

class ClockSyncEstimator {
public:
    explicit ClockSyncEstimator(size_t windowSize = 9);

    void Reset();

    // Returns false for causally invalid observations or negative network RTT.
    bool Observe(const ClockSyncObservation& observation);

    [[nodiscard]] std::optional<ClockSyncEstimate> Estimate() const;

    // Converts an HMD monotonic timestamp into the PC monotonic clock domain.
    // Returns nullopt until at least one valid sync observation exists.
    [[nodiscard]] std::optional<int64_t> HmdToPcMonotonicNs(int64_t hmdMonotonicNs) const;

private:
    size_t windowSize_ = 9;
    std::deque<ClockSyncEstimate> samples_;
};

}  // namespace pico_ot::bridge
