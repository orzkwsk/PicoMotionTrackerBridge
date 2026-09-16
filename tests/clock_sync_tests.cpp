#include "pico_ot_bridge/clock_sync.hpp"

#include "require.hpp"
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

    REQUIRE(estimator.Observe(observation));
    const auto estimate = estimator.Estimate();
    REQUIRE(estimate.has_value());
    REQUIRE(estimate->hmdMinusPcNs == 5'000'000'000ll);
    REQUIRE(estimate->networkRoundTripNs == 4'000'000ll);

    const auto mapped = estimator.HmdToPcMonotonicNs(10'000'000'000ll);
    REQUIRE(mapped.has_value());
    REQUIRE(*mapped == 5'000'000'000ll);
}

void TestLowestRttWins() {
    ClockSyncEstimator estimator(3);

    // Same underlying +5 s offset, but asymmetric queueing biases this sample.
    REQUIRE(estimator.Observe({
        2'000'000'000ll,
        7'009'000'000ll,
        7'010'000'000ll,
        2'012'000'000ll,
    }));

    // Lower RTT, symmetric sample should become the active estimate.
    REQUIRE(estimator.Observe({
        3'000'000'000ll,
        8'001'000'000ll,
        8'002'000'000ll,
        3'003'000'000ll,
    }));

    const auto estimate = estimator.Estimate();
    REQUIRE(estimate.has_value());
    REQUIRE(estimate->hmdMinusPcNs == 5'000'000'000ll);
    REQUIRE(estimate->networkRoundTripNs == 2'000'000ll);
}

void TestInvalidObservation() {
    ClockSyncEstimator estimator;
    REQUIRE(!estimator.Estimate().has_value());
    REQUIRE(!estimator.HmdToPcMonotonicNs(123).has_value());

    // Client receive predates client send.
    REQUIRE(!estimator.Observe({100, 200, 210, 90}));

    // Server processing interval exceeds complete client round trip.
    REQUIRE(!estimator.Observe({100, 150, 300, 200}));
    REQUIRE(!estimator.Estimate().has_value());
}

}  // namespace

int main() {
    TestSymmetricExchange();
    TestLowestRttWins();
    TestInvalidObservation();
    return 0;
}
