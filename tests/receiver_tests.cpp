#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/receiver.hpp"

#include <cassert>
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace pico_ot::bridge;

namespace {

void PumpPair(Publisher& publisher, Receiver& receiver, int iterations = 10) {
    std::string error;
    for (int i = 0; i < iterations; ++i) {
        assert(receiver.Pump(&error));
        assert(publisher.PumpControl(&error));
        assert(receiver.Pump(&error));
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

pico_ot::TrackerState MakePose(const char* serial, double x) {
    pico_ot::TrackerState state;
    state.serial = serial;
    state.runtimeDeviceId = 4;
    state.connectedSnapshot = true;
    state.positionValid = true;
    state.orientationSamplePresent = true;
    state.mappedMonotonicValid = true;
    state.sourceTimestamp = 123;
    state.observedMonotonicNs = MonotonicNowNs();
    state.mappedMonotonicNs = state.observedMonotonicNs;
    state.positionMeters = {x, 2.0, 3.0};
    state.orientationXyzw = {0.0, 0.0, 0.0, 1.0};
    state.frame = pico_ot::ReferenceFrame::PicoOutputA;
    state.angularVelocityFrame = pico_ot::AngularVelocityFrame::DeviceLocalCandidate;
    return state;
}

void TestLatestStateAndClockSync() {
    Receiver receiver;
    ReceiverConfig receiverConfig;
    receiverConfig.listenPort = 0;
    receiverConfig.timeSyncIntervalNs = 1'000'000LL;
    receiverConfig.staleTimeoutNs = 50'000'000LL;
    receiverConfig.clientSessionId = 0xabc;
    std::string error;
    assert(receiver.Start(receiverConfig, &error));

    Publisher publisher;
    PublisherConfig publisherConfig;
    publisherConfig.pcEndpoint = {"127.0.0.1", receiver.ListenPort()};
    publisherConfig.senderSessionId = 0x1111;
    assert(publisher.Start(publisherConfig, &error));

    auto pose = MakePose("PC2310MLK7200684G", 1.0);
    assert(publisher.SendPose(pose, &error));
    PumpPair(publisher, receiver, 5);

    const auto* first = receiver.Find(pose.serial);
    assert(first != nullptr);
    assert(first->pose.positionMeters.x == 1.0);
    assert(first->senderSessionId == 0x1111);
    assert(!receiver.IsPoseStale(*first, MonotonicNowNs()));

    pose.positionMeters.x = 4.0;
    pose.mappedMonotonicNs = MonotonicNowNs();
    assert(publisher.SendPose(pose, &error));
    PumpPair(publisher, receiver, 5);

    const auto* latest = receiver.Find(pose.serial);
    assert(latest != nullptr);
    assert(latest->pose.positionMeters.x == 4.0);

    // After at least one request/response exchange, the HMD pose timestamp can
    // be represented in the PC monotonic domain.
    PumpPair(publisher, receiver, 10);
    assert(receiver.ClockEstimate().has_value());
    latest = receiver.Find(pose.serial);
    assert(latest != nullptr);
    assert(latest->posePcMonotonicNs.has_value());

    assert(receiver.IsPoseStale(
        *latest, latest->lastPoseReceivePcNs + receiverConfig.staleTimeoutNs + 1));
}

void TestSenderSessionResetsTopology() {
    Receiver receiver;
    ReceiverConfig receiverConfig;
    receiverConfig.listenPort = 0;
    receiverConfig.clientSessionId = 0xdef;
    std::string error;
    assert(receiver.Start(receiverConfig, &error));

    Publisher publisherA;
    PublisherConfig a;
    a.pcEndpoint = {"127.0.0.1", receiver.ListenPort()};
    a.senderSessionId = 0xaaaa;
    assert(publisherA.Start(a, &error));
    assert(publisherA.SendPose(MakePose("TRACKER_A", 1.0), &error));
    PumpPair(publisherA, receiver, 2);
    assert(receiver.Find("TRACKER_A") != nullptr);

    publisherA.Stop();

    Publisher publisherB;
    PublisherConfig b;
    b.pcEndpoint = {"127.0.0.1", receiver.ListenPort()};
    b.senderSessionId = 0xbbbb;
    assert(publisherB.Start(b, &error));
    assert(publisherB.SendPose(MakePose("TRACKER_B", 2.0), &error));
    PumpPair(publisherB, receiver, 2);

    assert(receiver.ActiveSenderSessionId() == 0xbbbb);
    assert(receiver.Find("TRACKER_A") == nullptr);
    assert(receiver.Find("TRACKER_B") != nullptr);
}

}  // namespace

int main() {
    TestLatestStateAndClockSync();
    TestSenderSessionResetsTopology();
    return 0;
}
