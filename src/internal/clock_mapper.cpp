#include "internal/clock_mapper.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace pico_ot::internal {

ClockMapper::ClockMapper(size_t windowSamples, int64_t discontinuityThresholdNs)
    : windowSamples_(std::max<size_t>(1, windowSamples)),
      discontinuityThresholdNs_(std::max<int64_t>(1, discontinuityThresholdNs)) {}

void ClockMapper::Reset() {
    offsets_.clear();
    lastSourceNs_ = 0;
    hasLastSource_ = false;
}

int64_t ClockMapper::MedianOffset() const {
    std::vector<int64_t> copy(offsets_.begin(), offsets_.end());
    std::sort(copy.begin(), copy.end());
    return copy[copy.size() / 2];
}

bool ClockMapper::Observe(uint64_t sourceNs, int64_t observedMonotonicNs, int64_t& mappedMonotonicNs) {
    if (sourceNs == 0 || observedMonotonicNs <= 0) return false;

    if (hasLastSource_ && sourceNs <= lastSourceNs_) {
        Reset();
    }
    lastSourceNs_ = sourceNs;
    hasLastSource_ = true;

    const int64_t candidate = observedMonotonicNs - static_cast<int64_t>(sourceNs);
    if (!offsets_.empty()) {
        const int64_t current = MedianOffset();
        if (std::llabs(candidate - current) > discontinuityThresholdNs_) {
            offsets_.clear();
        }
    }

    offsets_.push_back(candidate);
    while (offsets_.size() > windowSamples_) offsets_.pop_front();

    mappedMonotonicNs = static_cast<int64_t>(sourceNs) + MedianOffset();
    return true;
}

}  // namespace pico_ot::internal
