#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/receiver.hpp"

#include "require.hpp"
#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

using namespace pico_ot::bridge;

namespace {

void PumpPair(Publisher& publisher, Receiver& receiver, int iterations = 10) {
    std::string error;
    for (int i = 0; i < iterations; ++i) {
        REQUIRE(receiver.Pump(&error));
        REQUIRE(publisher.PumpControl(&error));
        REQUIRE(receiver.Pump(&error));
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
    // Do not make this test depend on Windows scheduler granularity. The stale
    // transition itself is verified below with an explicit synthetic timestamp.
    receiverConfig.staleTimeoutNs = 1'000'000'000LL;
    receiverConfig.clientSessionId = 0xabc;
    std::string error;
    REQUIRE(receiver.Start(receiverConfig, &error));

    Publisher publisher;
    PublisherConfig publisherConfig;
    publisherConfig.pcEndpoint = {"127.0.0.1", receiver.ListenPort()};
    publisherConfig.senderSessionId = 0x1111;
    REQUIRE(publisher.Start(publisherConfig, &error));

    auto pose = MakePose("PC2310MLK7200684G", 1.0);
    REQUIRE(publisher.SendPose(pose, &error));
    PumpPair(publisher, receiver, 5);

    const auto* first = receiver.Find(pose.serial);
    REQUIRE(first != nullptr);
    REQUIRE(first->pose.positionMeters.x == 1.0);
    REQUIRE(first->senderSessionId == 0x1111);
    REQUIRE(!receiver.IsPoseStale(*first, MonotonicNowNs()));

    pose.positionMeters.x = 4.0;
    pose.mappedMonotonicNs = MonotonicNowNs();
    REQUIRE(publisher.SendPose(pose, &error));
    PumpPair(publisher, receiver, 5);

    const auto* latest = receiver.Find(pose.serial);
    REQUIRE(latest != nullptr);
    REQUIRE(latest->pose.positionMeters.x == 4.0);

    // After at least one request/response exchange, the HMD pose timestamp can
    // be represented in the PC monotonic domain.
    PumpPair(publisher, receiver, 10);
    REQUIRE(receiver.ClockEstimate().has_value());
    latest = receiver.Find(pose.serial);
    REQUIRE(latest != nullptr);
    REQUIRE(latest->posePcMonotonicNs.has_value());

    REQUIRE(receiver.IsPoseStale(
        *latest, latest->lastPoseReceivePcNs + receiverConfig.staleTimeoutNs + 1));
}

void TestSenderSessionResetsTopology() {
    Receiver receiver;
    ReceiverConfig receiverConfig;
    receiverConfig.listenPort = 0;
    receiverConfig.clientSessionId = 0xdef;
    std::string error;
    REQUIRE(receiver.Start(receiverConfig, &error));

    Publisher publisherA;
    PublisherConfig a;
    a.pcEndpoint = {"127.0.0.1", receiver.ListenPort()};
    a.senderSessionId = 0xaaaa;
    REQUIRE(publisherA.Start(a, &error));
    REQUIRE(publisherA.SendPose(MakePose("TRACKER_A", 1.0), &error));
    PumpPair(publisherA, receiver, 2);
    REQUIRE(receiver.Find("TRACKER_A") != nullptr);

    publisherA.Stop();

    Publisher publisherB;
    PublisherConfig b;
    b.pcEndpoint = {"127.0.0.1", receiver.ListenPort()};
    b.senderSessionId = 0xbbbb;
    REQUIRE(publisherB.Start(b, &error));
    REQUIRE(publisherB.SendPose(MakePose("TRACKER_B", 2.0), &error));
    PumpPair(publisherB, receiver, 2);

    REQUIRE(receiver.ActiveSenderSessionId() == 0xbbbb);
    REQUIRE(receiver.Find("TRACKER_A") == nullptr);
    REQUIRE(receiver.Find("TRACKER_B") != nullptr);
}

void TestBoundedPumpAndIdentityAdmission() {
    Receiver receiver;
    ReceiverConfig config;
    config.listenPort = 0;
    config.maxPacketsPerPump = 1;
    config.maxTrackers = 2;
    config.rejectRetiredSessions = true;
    std::string error;
    REQUIRE(receiver.Start(config, &error));
    Publisher publisher;
    PublisherConfig source;
    source.pcEndpoint = {"127.0.0.1", receiver.ListenPort()};
    source.senderSessionId = 123;
    REQUIRE(publisher.Start(source, &error));
    REQUIRE(publisher.SendPose(MakePose("A", 1), &error));
    REQUIRE(publisher.SendPose(MakePose("B", 2), &error));
    REQUIRE(publisher.SendPose(MakePose("C", 3), &error));
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    REQUIRE(receiver.Pump(&error));
    REQUIRE(receiver.Serials().size() == 1);
    REQUIRE(receiver.Pump(&error));
    REQUIRE(receiver.Serials().size() == 2);
    REQUIRE(receiver.Pump(&error));
    REQUIRE(receiver.Find("C") == nullptr);
    REQUIRE(publisher.SendPose(MakePose("A", 4), &error));
    PumpPair(publisher, receiver);
    REQUIRE(receiver.Find("A")->pose.positionMeters.x == 4);
    config.maxPacketsPerPump = 0;
    REQUIRE(!receiver.Start(config, &error));
}

}  // namespace

int main() {
    TestLatestStateAndClockSync();
    TestSenderSessionResetsTopology();
    TestBoundedPumpAndIdentityAdmission();
    return 0;
}
