#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>

namespace pico_ot::internal {

class ClockMapper {
public:
    ClockMapper(size_t windowSamples, int64_t discontinuityThresholdNs);

    void Reset();
    bool Observe(uint64_t sourceNs, int64_t observedMonotonicNs, int64_t& mappedMonotonicNs);

private:
    int64_t MedianOffset() const;

    size_t windowSamples_;
    int64_t discontinuityThresholdNs_;
    std::deque<int64_t> offsets_;
    uint64_t lastSourceNs_ = 0;
    bool hasLastSource_ = false;
};

}  // namespace pico_ot::internal
