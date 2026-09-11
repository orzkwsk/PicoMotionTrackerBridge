#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/receiver.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

int main(int argc, char** argv) {
    uint16_t port = 29765;
    if (argc >= 2) {
        const long parsed = std::strtol(argv[1], nullptr, 10);
        if (parsed <= 0 || parsed > 65535) {
            std::cerr << "Invalid port\n";
            return 2;
        }
        port = static_cast<uint16_t>(parsed);
    }

    pico_ot::bridge::Receiver receiver;
    pico_ot::bridge::ReceiverConfig config;
    config.listenPort = port;

    std::string error;
    if (!receiver.Start(config, &error)) {
        std::cerr << "Receiver start failed: " << error << "\n";
        return 1;
    }

    std::cout << "PICO OT bridge receiver listening on UDP "
              << receiver.ListenPort() << "\n";
    std::cout << "Press Ctrl+C to stop.\n";

    uint64_t lastSession = 0;
    int64_t lastPrintNs = 0;
    std::unordered_map<std::string, uint32_t> lastSequences;

    for (;;) {
        if (!receiver.Pump(&error)) {
            std::cerr << "Receiver error: " << error << "\n";
            return 1;
        }

        const int64_t now = pico_ot::bridge::MonotonicNowNs();
        if (receiver.ActiveSenderSessionId() != 0 &&
            receiver.ActiveSenderSessionId() != lastSession) {
            lastSession = receiver.ActiveSenderSessionId();
            lastSequences.clear();
            std::cout << "\nSender session: " << lastSession << "\n";
        }

        if (lastPrintNs == 0 || now - lastPrintNs >= 1'000'000'000LL) {
            lastPrintNs = now;
            const auto clock = receiver.ClockEstimate();
            if (clock) {
                std::cout << "clock hmd-pc="
                          << std::fixed << std::setprecision(3)
                          << static_cast<double>(clock->hmdMinusPcNs) / 1.0e6
                          << " ms rtt="
                          << static_cast<double>(clock->networkRoundTripNs) / 1.0e6
                          << " ms\n";
            }

            for (const auto& serial : receiver.Serials()) {
                const auto* state = receiver.Find(serial);
                if (state == nullptr || !state->hasPoseSequence) {
                    continue;
                }

                const bool stale = receiver.IsPoseStale(*state, now);
                std::cout
                    << serial
                    << " seq=" << state->lastPoseSequence
                    << " stale=" << (stale ? 1 : 0)
                    << " posValid=" << (state->pose.positionValid ? 1 : 0)
                    << " ori=" << (state->pose.orientationSamplePresent ? 1 : 0)
                    << " p=(" << std::setprecision(4)
                    << state->pose.positionMeters.x << ","
                    << state->pose.positionMeters.y << ","
                    << state->pose.positionMeters.z << ")";
                if (state->posePcMonotonicNs) {
                    const double ageMs =
                        static_cast<double>(now - *state->posePcMonotonicNs) / 1.0e6;
                    std::cout << " age=" << std::setprecision(2) << ageMs << " ms";
                }
                std::cout << "\n";
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
