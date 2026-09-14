#include "pico_ot_backend/observation_adapter.hpp"
#include "pico_ot_backend/observation_identity.hpp"
#include "pico_ot_backend/observation_publisher.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace p = monaka::protocol::v1;
namespace b = pico_ot::bridge;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Rig {
    b::Receiver receiver;
    b::UdpSocket sender;
    int64_t epoch=b::MonotonicNowNs();
    uint32_t revision=10;
    pico_ot::ObservationConfig Config(size_t limit=256) {
        pico_ot::ObservationConfig c;
        c.sourceId="persistent-pico-source"; c.spaceId="configured-map"; c.spaceRevision=revision;
        c.clockEpochPcNs=epoch; c.newUuid=pico_ot::NewObservationUuid;
        c.nextSpaceRevision=[this] { return ++revision; }; c.maxDevices=limit; return c;
    }
    Rig() {
        b::ReceiverConfig c; c.listenPort=0; c.exclusiveBind=true; c.rejectRetiredSessions=true;
        std::string error; Check(receiver.Start(c,&error),error.c_str());
        Check(sender.Open(0,true,&error),error.c_str());
    }
    void Pump() {
        for (int i=0;i<3;++i) { std::string e; Check(receiver.Pump(&e),e.c_str()); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
    }
    void Send(const std::vector<uint8_t>& data) {
        std::string e; Check(sender.SendTo({"127.0.0.1",receiver.ListenPort()},data.data(),data.size(),&e),e.c_str()); Pump();
    }
    void Pose(uint32_t sequence, b::PosePacket pose, uint64_t session=101) {
        std::vector<uint8_t> data; Check(b::EncodePosePacket(session,sequence,pose,data),"encode POTB pose"); Send(data);
    }
    void Device(uint32_t sequence, const std::string& serial, uint64_t session=101) {
        b::DeviceStatePacket device; device.serial=serial; device.connectedSnapshot=true;
        device.charging=true; device.batteryBucketPresent=true; device.batteryBucketRaw=7;
        std::vector<uint8_t> data; Check(b::EncodeDeviceStatePacket(session,sequence,device,data),"encode POTB state"); Send(data);
    }
};
b::PosePacket Pose(const std::string& serial="FULL-SERIAL-01") {
    b::PosePacket pose; pose.serial=serial; pose.runtimeDeviceId=345678;
    pose.connectedSnapshot=true; pose.positionValid=true; pose.orientationSamplePresent=true;
    pose.positionMeters={1,2,3}; pose.orientationXyzw={0,0,0,1.2};
    pose.linearVelocityMetersPerSec={4,5,6}; pose.angularVelocityRadPerSec={7,8,9};
    pose.mappedMonotonicValid=true; pose.mappedMonotonicNs=123456789;
    pose.observedMonotonicNs=987654321; pose.rawTrackingState65=4;
    return pose;
}
template<class T> const T& Find(const std::vector<p::Envelope>& values) {
    for (const auto& value:values) if (const auto* v=std::get_if<T>(&value)) return *v;
    throw std::runtime_error("expected message missing");
}
void RoundTrip(const std::vector<p::Envelope>& values) {
    for (const auto& value:values) {
        std::string wire; p::Error error; p::Envelope decoded;
        Check(p::EncodeEnvelope(value,wire,error),error.message.c_str());
        Check(wire.find("345678")==std::string::npos,"runtime ID leaked");
        Check(wire.find("batteryBucket")==std::string::npos,"raw bucket leaked");
        Check(p::DecodeEnvelope(reinterpret_cast<const uint8_t*>(wire.data()),wire.size(),decoded,error),error.message.c_str());
    }
}
void MappingAndStreams() {
    Rig r; pico_ot::PicoObservationAdapter a(r.Config());
    r.Pose(10,Pose()); r.Device(10,"FULL-SERIAL-01");
    auto values=a.Collect(r.receiver,b::MonotonicNowNs()); Check(values.size()==2,"separate pose/state streams"); RoundTrip(values);
    const auto pose=Find<p::TrackerObservation>(values);
    Check(pose.sequence==0 && pose.timestamp_ns==r.receiver.Find(pose.device_id)->lastPoseReceivePcNs-r.epoch,"immutable admission time");
    p::Envelope retransmit=pose;
    Check(a.StampSentAt(retransmit,r.epoch+pose.sent_at_ns+123),"stamp encoding time");
    Check(std::get<p::TrackerObservation>(retransmit).timestamp_ns==pose.timestamp_ns &&
          std::get<p::TrackerObservation>(retransmit).sent_at_ns==pose.sent_at_ns+123,"encoding recreated sample time");
    Check(pose.tracking_state=="tracked" && pose.orientation_evidence=="sample_sanity","mapping evidence");
    Check(pose.orientation->at(3)==1.2,"backend must preserve quaternion");
    Check(pose.linear_velocity && !pose.angular_velocity && !pose.linear_acceleration,"candidate derivative exclusion");
    Check(pose.battery && !pose.battery->fraction && !pose.battery->charging,"unknown bucket/charging");
    const auto state=Find<p::ObservationDeviceState>(values);
    Check(state.presence=="unknown" && state.tracking_state=="unknown" && state.sequence==0,"startup snapshot is not live presence");
    r.Pose(10,Pose()); r.Pose(9,Pose());
    Check(a.Collect(r.receiver,b::MonotonicNowNs()).empty(),"duplicate/reordered pose re-emitted");
    r.Device(11,pose.device_id);
    auto metadata=a.Collect(r.receiver,b::MonotonicNowNs());
    Check(metadata.size()==1 && Find<p::ObservationDeviceState>(metadata).sequence==1,"metadata changed pose sequence");
    Check(r.receiver.Find(pose.device_id)->lastPoseReceivePcNs-r.epoch==pose.timestamp_ns,"metadata refreshed pose");
    auto invalid=Pose(); invalid.positionValid=false; invalid.positionMeters={999,22,33};
    r.Pose(11,invalid); values=a.Collect(r.receiver,b::MonotonicNowNs()); RoundTrip(values);
    const auto& partial=Find<p::TrackerObservation>(values);
    Check(!partial.validity.position && partial.validity.orientation && partial.tracking_state=="degraded","independent validity");
    Check(partial.position->at(0)==999 && !partial.linear_velocity && partial.sequence==1,"diagnostics/velocity/independent sequence");
    invalid.positionValid=true; invalid.orientationSamplePresent=false; r.Pose(12,invalid);
    values=a.Collect(r.receiver,b::MonotonicNowNs()); RoundTrip(values);
    Check(Find<p::TrackerObservation>(values).tracking_state=="degraded","position-only");
    invalid.positionValid=false; r.Pose(13,invalid); values=a.Collect(r.receiver,b::MonotonicNowNs()); RoundTrip(values);
    Check(Find<p::TrackerObservation>(values).tracking_state=="lost","both components invalid");
}
void SessionAndWrap() {
    Rig r; pico_ot::PicoObservationAdapter a(r.Config());
    r.Pose(0xffffffff,Pose()); auto first=a.Collect(r.receiver,b::MonotonicNowNs());
    const auto original=Find<p::TrackerObservation>(first);
    r.Pose(0,Pose()); auto wrap=a.Collect(r.receiver,b::MonotonicNowNs());
    Check(Find<p::TrackerObservation>(wrap).sequence==1,"POTB wrap must not wrap C1");
    r.Pose(0,Pose("NEW-SERIAL"),202); auto reset=a.Collect(r.receiver,b::MonotonicNowNs()); RoundTrip(reset);
    const auto& next=Find<p::TrackerObservation>(reset);
    Check(next.session_id!=original.session_id && next.clock_id==original.clock_id && next.sequence==0,"session reset/clock identity");
    Check(next.coordinate_space.revision==original.coordinate_space.revision+1,"map revision reset");
    Check(!r.receiver.Find(original.device_id),"old device cache survived");
    r.Pose(300,Pose(),101);
    Check(r.receiver.ActiveSenderSessionId()==202 && a.Collect(r.receiver,b::MonotonicNowNs()).empty(),"retired session resurrected");
}
void ResyncAndStale() {
    Rig r; pico_ot::PicoObservationAdapter a(r.Config()); r.Pose(1,Pose());
    auto first=a.Collect(r.receiver,b::MonotonicNowNs()); const auto original=Find<p::TrackerObservation>(first);
    std::array<uint8_t,512> data{}; size_t size=0; b::UdpEndpoint from;
    bool synced=false;
    while (r.sender.ReceiveFrom(data.data(),data.size(),size,from)==b::UdpReceiveStatus::Packet) {
        b::DecodedPacket decoded;
        Check(b::DecodePacket(data.data(),size,decoded)==b::DecodeStatus::Ok,"time sync request decode");
        if (const auto* request=std::get_if<b::TimeSyncRequestPacket>(&decoded.payload)) {
            b::TimeSyncResponsePacket response;
            response.nonce=request->nonce; response.clientSendMonotonicNs=request->clientSendMonotonicNs;
            response.serverReceiveMonotonicNs=request->clientSendMonotonicNs-5'000'000'000LL;
            response.serverSendMonotonicNs=response.serverReceiveMonotonicNs+100;
            std::vector<uint8_t> bytes; Check(b::EncodeTimeSyncResponsePacket(101,1,response,bytes),"time sync response encode");
            r.Send(bytes); synced=true;
        }
    }
    Check(synced && r.receiver.ClockEstimate().has_value(),"clock resync did not occur");
    Check(a.Collect(r.receiver,b::MonotonicNowNs()).empty(),"resync emitted old pose");
    Check(original.timestamp_ns==r.receiver.Find(original.device_id)->lastPoseReceivePcNs-r.epoch,"resync changed age");
    r.Pose(2,Pose()); const auto time=r.receiver.Find(original.device_id)->lastPoseReceivePcNs;
    auto stale=a.Collect(r.receiver,time+500'000'001); RoundTrip(stale);
    const auto& pose=Find<p::TrackerObservation>(stale);
    Check(!pose.validity.position && !pose.validity.orientation && pose.tracking_state=="lost","delayed admission freshness");
    Check(pose.timestamp_ns==time-r.epoch && pose.sent_at_ns-pose.timestamp_ns==500'000'001,"stale timestamp recreated");
}
void CountsAndBind() {
    Rig r; b::UdpSocket competing; std::string error;
    Check(!competing.Open(r.receiver.ListenPort(),true,&error),"duplicate ingress bind allowed");
    pico_ot::PicoObservationAdapter a(r.Config());
    for (uint32_t i=0;i<8;++i) r.Pose(i,Pose("SERIAL-"+std::to_string(i)));
    auto values=a.Collect(r.receiver,b::MonotonicNowNs()); Check(values.size()==8,"five/variable device count"); RoundTrip(values);
    pico_ot::PicoObservationAdapter limited(r.Config(3));
    Check(limited.Collect(r.receiver,b::MonotonicNowNs()).size()==3 && limited.RejectedSamples()==5,"bounded provider cache");
}
void PublisherAndLegacy() {
    Rig r; pico_ot::PicoObservationAdapter adapter(r.Config()); pico_ot::ObservationPublisher publisher;
    b::UdpSocket consumer; std::string error; Check(consumer.Open(0,true,&error,true,true),error.c_str());
    const auto port=consumer.BoundPort(); Check(publisher.Start(port,&error),error.c_str());
    r.Pose(1,Pose()); publisher.Publish(adapter,r.receiver,b::MonotonicNowNs());
    std::array<uint8_t,4096> bytes{}; size_t size=0; b::UdpEndpoint source;
    Check(consumer.ReceiveFrom(bytes.data(),bytes.size(),size,source)==b::UdpReceiveStatus::Packet,"missing C1 UDP");
    p::Envelope decoded; p::Error codecError;
    Check(p::DecodeEnvelope(bytes.data(),size,decoded,codecError),codecError.message.c_str());
    consumer.Close();
    b::Receiver legacy; b::ReceiverConfig rc; rc.listenPort=0; Check(legacy.Start(rc,&error),error.c_str());
    b::Publisher old; b::PublisherConfig pc; pc.pcEndpoint={"127.0.0.1",legacy.ListenPort()}; Check(old.Start(pc,&error),error.c_str());
    for (uint32_t i=2;i<7;++i) {
        r.Pose(i,Pose()); publisher.Publish(adapter,r.receiver,b::MonotonicNowNs());
        pico_ot::TrackerState state; state.serial="LEGACY"; state.positionValid=true; state.orientationSamplePresent=true;
        Check(old.SendPose(state,&error),error.c_str()); Check(legacy.Pump(&error),error.c_str());
    }
    Check(legacy.Find("LEGACY") && legacy.Find("LEGACY")->lastPoseSequence==4,"stopped C1 consumer stalled legacy stream");
}
void Identity() {
    const auto path=std::filesystem::current_path()/("identity-test-"+pico_ot::NewObservationUuid()+".conf");
    std::string source,space; uint32_t revision;
    { pico_ot::ObservationIdentity first(path); source=first.SourceId(); space=first.SpaceId(); revision=first.Revision(); }
    { pico_ot::ObservationIdentity next(path); Check(next.SourceId()==source && next.SpaceId()==space && next.Revision()==revision+1,"restart identity/revision"); }
    Check(std::filesystem::remove(path),"identity cleanup");
}
int main() {
    try { MappingAndStreams(); SessionAndWrap(); ResyncAndStale(); CountsAndBind(); PublisherAndLegacy(); Identity(); }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
    std::cout << "PASS observation mapping, streams, resync/age, wrap/session, bounded cache, single bind, UDP/legacy, persistent identity\n";
}
