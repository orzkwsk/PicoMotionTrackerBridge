#include "pico_ot_bridge/clock_sync.hpp"

#include <cassert>
#include <cstdint>

using namespace pico_ot::bridge;

namespace {

void TestSymmetricExchange() {
    ClockSyncEstimator estimator;

    // HMD monotonic clock is exactly PC + 5 seconds.
    // PC -> HMD: 2 ms, HMD processing: 1 ms, HMD -> PC: 2 ms.
    const ClockSyncObservation observation{
        1'000'000'000ll,  // t0 PC
        6'002'000'000ll,  // t1 HMD
        6'003'000'000ll,  // t2 HMD
        1'005'000'000ll,  // t3 PC
    };

    assert(estimator.Observe(observation));
    const auto estimate = estimator.Estimate();
    assert(estimate.has_value());
    assert(estimate->hmdMinusPcNs == 5'000'000'000ll);
    assert(estimate->networkRoundTripNs == 4'000'000ll);

    const auto mapped = estimator.HmdToPcMonotonicNs(10'000'000'000ll);
    assert(mapped.has_value());
    assert(*mapped == 5'000'000'000ll);
}

void TestLowestRttWins() {
    ClockSyncEstimator estimator(3);

    // Same underlying +5 s offset, but asymmetric queueing biases this sample.
    assert(estimator.Observe({
        2'000'000'000ll,
        7'009'000'000ll,
        7'010'000'000ll,
        2'012'000'000ll,
    }));

    // Lower RTT, symmetric sample should become the active estimate.
    assert(estimator.Observe({
        3'000'000'000ll,
        8'001'000'000ll,
        8'002'000'000ll,
        3'003'000'000ll,
    }));

    const auto estimate = estimator.Estimate();
    assert(estimate.has_value());
    assert(estimate->hmdMinusPcNs == 5'000'000'000ll);
    assert(estimate->networkRoundTripNs == 2'000'000ll);
}

void TestInvalidObservation() {
    ClockSyncEstimator estimator;
    assert(!estimator.Estimate().has_value());
    assert(!estimator.HmdToPcMonotonicNs(123).has_value());

    // Client receive predates client send.
    assert(!estimator.Observe({100, 200, 210, 90}));

    // Server processing interval exceeds complete client round trip.
    assert(!estimator.Observe({100, 150, 300, 200}));
    assert(!estimator.Estimate().has_value());
}

}  // namespace

int main() {
    TestSymmetricExchange();
    TestLowestRttWins();
    TestInvalidObservation();
    return 0;
}
