#include "pico_ot_bridge/clock_sync.hpp"

#include <algorithm>
#include <limits>

namespace pico_ot::bridge {

ClockSyncEstimator::ClockSyncEstimator(size_t windowSize)
    : windowSize_(std::max<size_t>(1, windowSize)) {}

void ClockSyncEstimator::Reset() {
    samples_.clear();
}

bool ClockSyncEstimator::Observe(const ClockSyncObservation& o) {
    if (o.clientReceiveMonotonicNs < o.clientSendMonotonicNs ||
        o.serverSendMonotonicNs < o.serverReceiveMonotonicNs) {
        return false;
    }

    const int64_t clientElapsed = o.clientReceiveMonotonicNs - o.clientSendMonotonicNs;
    const int64_t serverElapsed = o.serverSendMonotonicNs - o.serverReceiveMonotonicNs;
    if (serverElapsed > clientElapsed) {
        return false;
    }

    const int64_t networkRoundTripNs = clientElapsed - serverElapsed;

    // NTP offset estimate:
    //   theta = ((t1 - t0) + (t2 - t3)) / 2
    // Here theta is HMD minus PC monotonic time.
    const int64_t a = o.serverReceiveMonotonicNs - o.clientSendMonotonicNs;
    const int64_t b = o.serverSendMonotonicNs - o.clientReceiveMonotonicNs;

    // Avoid overflowing a+b even though practical monotonic offsets are much smaller.
    const int64_t halfA = a / 2;
    const int64_t halfB = b / 2;
    const int64_t remainder = (a % 2 + b % 2) / 2;
    const int64_t offsetNs = halfA + halfB + remainder;

    samples_.push_back({offsetNs, networkRoundTripNs});
    while (samples_.size() > windowSize_) {
        samples_.pop_front();
    }
    return true;
}

std::optional<ClockSyncEstimate> ClockSyncEstimator::Estimate() const {
    if (samples_.empty()) {
        return std::nullopt;
    }

    // For a short rolling window, the lowest network RTT sample is the least
    // contaminated by queueing delay and is a useful first-order NTP estimate.
    const auto it = std::min_element(
        samples_.begin(), samples_.end(),
        [](const ClockSyncEstimate& lhs, const ClockSyncEstimate& rhs) {
            return lhs.networkRoundTripNs < rhs.networkRoundTripNs;
        });
    return *it;
}

std::optional<int64_t> ClockSyncEstimator::HmdToPcMonotonicNs(int64_t hmdMonotonicNs) const {
    const auto estimate = Estimate();
    if (!estimate) {
        return std::nullopt;
    }

    if ((estimate->hmdMinusPcNs > 0 &&
         hmdMonotonicNs < std::numeric_limits<int64_t>::min() + estimate->hmdMinusPcNs) ||
        (estimate->hmdMinusPcNs < 0 &&
         hmdMonotonicNs > std::numeric_limits<int64_t>::max() + estimate->hmdMinusPcNs)) {
        return std::nullopt;
    }

    return hmdMonotonicNs - estimate->hmdMinusPcNs;
}

}  // namespace pico_ot::bridge
