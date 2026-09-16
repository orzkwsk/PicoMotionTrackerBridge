#include "internal/clock_mapper.hpp"
#include "internal/decoder.hpp"

#include "require.hpp"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>

namespace {

template <typename T>
void Put(uint8_t* dst, size_t offset, const T& value) {
    std::memcpy(dst + offset, &value, sizeof(T));
}

void TestDecode() {
    uint8_t raw[pico_ot::internal::kOutputBufferSize];
    std::memset(raw, 0xA5, sizeof(raw));
    std::memset(raw, 0, pico_ot::internal::kObservedAlgoResultSize);

    const uint64_t ts = 123456789ULL;
    Put(raw, 0, ts);
    double p[3] = {1000.0, -2000.0, 500.0};
    double q[4] = {0.0, 0.0, 0.0, 1.0};
    double lv[3] = {10.0, 20.0, 30.0};
    double la[3] = {100.0, 200.0, 300.0};
    double av[3] = {0.1, 0.2, 0.3};
    std::memcpy(raw + 8, p, sizeof(p));
    std::memcpy(raw + 32, q, sizeof(q));
    std::memcpy(raw + 224, lv, sizeof(lv));
    std::memcpy(raw + 248, la, sizeof(la));
    std::memcpy(raw + 272, av, sizeof(av));
    raw[64] = 1;
    raw[65] = 3;

    pico_ot::RawTrackerState state{};
    const auto status = pico_ot::internal::DecodeOutputA(raw, sizeof(raw), 0, 999, state);
    REQUIRE(status.ok());
    REQUIRE(state.sourceTimestamp == ts);
    REQUIRE(state.positionValid);
    REQUIRE(state.orientationSamplePresent);
    REQUIRE(state.rawTrackingState65 == 3);
    REQUIRE(std::fabs(state.positionMm.x - 1000.0) < 1e-9);
    REQUIRE(std::fabs(state.angularVelocityRawRadPerSec.z - 0.3) < 1e-9);
    REQUIRE(pico_ot::internal::OutputTailUnmodified(raw, 392, sizeof(raw), 0xA5));
}

void TestClockMapper() {
    pico_ot::internal::ClockMapper mapper(5, 100'000'000LL);
    int64_t mapped = 0;
    REQUIRE(mapper.Observe(1'000'000'000ULL, 5'000'000'000LL, mapped));
    REQUIRE(mapped == 5'000'000'000LL);
    REQUIRE(mapper.Observe(1'010'000'000ULL, 5'010'001'000LL, mapped));
    REQUIRE(std::llabs(mapped - 5'010'000'000LL) < 2'000LL);
    REQUIRE(mapper.Observe(100ULL, 9'000'000'000LL, mapped));
    REQUIRE(mapped == 9'000'000'000LL);
}

}  // namespace

int main() {
    TestDecode();
    TestClockMapper();
    std::cout << "pico_ot_backend_tests: ok\n";
    return 0;
}
