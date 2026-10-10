// Diagnostic-only synthetic loopback; no HMD or service control.
#include "pico_ot_backend/runtime_diagnostic.hpp"
#include "pico_ot_backend/observation_adapter.hpp"
#include "pico_ot_backend/observation_identity.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
#include "require.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

namespace b=pico_ot::bridge;
namespace p=monaka::protocol::v2;
using Json=pico_ot::RuntimeDiagnostic::Json;
constexpr int64_t threshold=1'000'000'000LL;

struct Rig {
    b::Receiver receiver;
    b::UdpSocket sender;
    pico_ot::RuntimeDiagnostic diagnostic;
    std::vector<Json> events;
    uint32_t sequence=0;
    Rig() {
        b::ReceiverConfig config;config.listenPort=0;config.rejectRetiredSessions=true;
        REQUIRE(receiver.Start(config));REQUIRE(sender.Open(0,true));
        diagnostic.StartForTest([&](const std::string& line){events.push_back(Json::parse(line));},65536,1024);
    }
    ~Rig(){diagnostic.Stop();receiver.Stop();sender.Close();}
    int64_t Send(bool device=false,uint64_t token=101,const std::string& serial="RR00O-SYNTHETIC-A") {
        const auto before=b::MonotonicNowNs();while(b::MonotonicNowNs()<=before){}
        std::vector<uint8_t> bytes;const auto seq=sequence++;
        if(device){b::DeviceStatePacket packet;packet.serial=serial;packet.runtimeDeviceId=1;
            REQUIRE(b::EncodeDeviceStatePacket(token,seq,packet,bytes));}
        else {b::PosePacket packet;packet.serial=serial;packet.runtimeDeviceId=1;
            packet.connectedSnapshot=true;packet.positionValid=true;packet.orientationSamplePresent=true;
            packet.positionMeters={1,2,3};packet.orientationXyzw={0,0,0,1};
            REQUIRE(b::EncodePosePacket(token,seq,packet,bytes));}
        REQUIRE(sender.SendTo({"127.0.0.1",receiver.ListenPort()},bytes.data(),bytes.size()));
        for(int attempt=0;attempt<1000;++attempt){
            REQUIRE(receiver.Pump());auto* state=receiver.Find(serial);
            if(state&&receiver.ActiveSenderSessionId()==token&&
                (device?state->hasDeviceSequence&&state->lastDeviceSequence==seq:
                        state->hasPoseSequence&&state->lastPoseSequence==seq)){
                const auto received=device?state->lastDeviceReceivePcNs:state->lastPoseReceivePcNs;
                REQUIRE(received>before);return received;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("synthetic packet not admitted");
    }
    void Poll(int64_t now){diagnostic.Poll(receiver,now);}
    void End(){diagnostic.Stop();REQUIRE(diagnostic.Complete());}
    std::vector<Json> Select(const std::string& type,const std::string& reason="") const{
        std::vector<Json> rows;
        for(const auto& e:events)if(e["event_type"]==type&&(reason.empty()||e["details"].value("reason",std::string{})==reason))rows.push_back(e);
        return rows;
    }
    Json Result(const std::string& name)const{return {{"case",name},{"status","PASS"},{"events",events}};}
};

void Exact(const Json& event,int64_t now,int64_t pose,int64_t device){
    const auto& d=event["details"];const auto latest=(std::max)(pose,device);const auto age=now-latest;
    REQUIRE(d["diagnostic_poll_now_ns"]==now);REQUIRE(d["latest_receive_monotonic_ns"]==latest);
    REQUIRE(d["latest_pose_receive_monotonic_ns"]==pose);REQUIRE(d["latest_device_receive_monotonic_ns"]==device);
    REQUIRE(d["freshness_age_ns"].is_number_integer());REQUIRE(d["freshness_age_ns"]==age);
    REQUIRE(d["freshness_threshold_ns"]==threshold);REQUIRE(d["sender_present"]==true);
    REQUIRE(d["latest_present"]==(latest>0));REQUIRE(d["now_ge_latest"]==(now>=latest));
    REQUIRE(d["within_freshness_threshold"]==(now>=latest&&age<=threshold));
}

Json TrueStale(){
    Rig r;auto latest=r.Send();r.Poll(latest);const auto now=latest+threshold+123;
    r.Poll(now);r.Poll(now+threshold);r.End();
    auto rows=r.Select("session_disconnected","source_observation_timeout");REQUIRE(rows.size()==1);
    Exact(rows[0],now,latest,0);REQUIRE(rows[0]["details"]["latest_receive_kind"]=="pose");
    // Synthetic predicate now is in the future; emission time is deliberately different.
    REQUIRE(rows[0]["monotonic_ns"].get<int64_t>()!=now);
    return r.Result("true_stale");
}
Json Fresh(){
    Rig r;auto latest=r.Send();r.Poll(latest);r.Poll(latest+threshold);r.End();
    REQUIRE(r.Select("session_disconnected","source_observation_timeout").empty());
    auto rows=r.Select("session_connected");REQUIRE(rows.size()==1);Exact(rows[0],latest,latest,0);
    return r.Result("fresh_state_inclusive_threshold");
}
Json ClockOrder(){
    Rig r;auto latest=r.Send();r.Poll(latest);r.Poll(latest-123);r.End();
    auto rows=r.Select("session_disconnected","source_observation_timeout");REQUIRE(rows.size()==1);
    Exact(rows[0],latest-123,latest,0);REQUIRE(rows[0]["details"]["freshness_age_ns"]==-123);
    REQUIRE(rows[0]["details"]["now_ge_latest"]==false);return r.Result("clock_order_negative_age_unclamped");
}
Json Latest(bool device){
    Rig r;int64_t pose=0,metadata=0;
    if(device){pose=r.Send();metadata=r.Send(true,101,"RR00O-SYNTHETIC-B");REQUIRE(metadata>pose);}
    else {metadata=r.Send(true,101,"RR00O-SYNTHETIC-B");pose=r.Send();REQUIRE(pose>metadata);}
    auto latest=(std::max)(pose,metadata);r.Poll(latest);r.Poll(latest+threshold+1);r.End();
    auto connected=r.Select("session_connected");REQUIRE(connected.size()==1);Exact(connected[0],latest,pose,metadata);
    auto timeout=r.Select("session_disconnected","source_observation_timeout");REQUIRE(timeout.size()==1);
    Exact(timeout[0],latest+threshold+1,pose,metadata);
    REQUIRE(timeout[0]["details"]["latest_receive_kind"]==(device?"device_state":"pose"));
    return r.Result(device?"latest_device_across_serials":"latest_pose_across_serials");
}
const p::TrackerObservation& Pose(const std::vector<p::Envelope>& rows){
    for(const auto& row:rows)if(auto* pose=std::get_if<p::TrackerObservation>(&row))return *pose;
    throw std::runtime_error("synthetic Observation missing");
}
Json ReconnectAndRotation(){
    const auto path=std::filesystem::current_path()/("rr00o-identity-"+std::to_string(b::MonotonicNowNs())+".conf");
    REQUIRE(!std::filesystem::exists(path));Json result;
    {
        pico_ot::ObservationIdentity identity(path);const auto initial=identity.Revision();unsigned rotations=0;
        pico_ot::ObservationConfig config;config.sourceId=identity.SourceId();config.spaceId=identity.SpaceId();
        config.spaceRevision=initial;config.clockEpochPcNs=b::MonotonicNowNs();config.newUuid=pico_ot::NewObservationUuid;
        config.nextSpaceRevision=[&]{++rotations;return identity.AdvanceRevision();};
        pico_ot::PicoObservationAdapter adapter(config);Rig r;
        auto latest=r.Send();r.Poll(latest);auto first=Pose(adapter.Collect(r.receiver,b::MonotonicNowNs()));
        r.Poll(latest+threshold+1);auto recovered=r.Send();r.Poll(recovered);
        auto same=Pose(adapter.Collect(r.receiver,b::MonotonicNowNs()));
        REQUIRE(rotations==0&&identity.Revision()==initial&&same.session_id==first.session_id);
        auto rotatedReceive=r.Send(false,202);r.Poll(rotatedReceive);
        auto rotated=Pose(adapter.Collect(r.receiver,b::MonotonicNowNs()));
        REQUIRE(rotations==1&&identity.Revision()==initial+1&&rotated.coordinate_space.revision==initial+1);
        REQUIRE(rotated.session_id!=first.session_id&&rotated.source_id==first.source_id&&rotated.coordinate_space.id==first.coordinate_space.id);
        r.End();auto reconnect=r.Select("session_reconnecting");REQUIRE(reconnect.size()==2);
        Exact(reconnect[0],recovered,recovered,0);REQUIRE(reconnect[0]["details"]["sender_same_as_previous"]==true);
        Exact(reconnect[1],rotatedReceive,rotatedReceive,0);REQUIRE(reconnect[1]["details"]["sender_same_as_previous"]==false);
        REQUIRE(r.Select("session_disconnected","source_observation_timeout").size()==1);
        REQUIRE(r.Select("session_disconnected","sender_session_changed").size()==1);
        REQUIRE(r.Select("session_connected").size()==3);
        result=r.Result("same_token_reconnect_and_sender_rotation");
        result["same_token_revision_delta"]=0;result["sender_rotation_revision_delta"]=1;result["sender_rotation_count"]=rotations;
    }
    std::filesystem::remove(path);return result;
}

int main(int argc,char** argv){
    try{
        Json cases=Json::array({TrueStale(),Fresh(),ClockOrder(),Latest(true),Latest(false),ReconnectAndRotation()});
        Json result={{"status","PASS"},{"synthetic_loopback_only",true},{"production_receiver_and_diagnostic",true},{"cases",cases}};
        if(argc==2){std::ofstream file(argv[1],std::ios::binary);REQUIRE(file.good());file<<result.dump()<<'\n';REQUIRE(file.good());}
        std::cout<<"timeout attribution tests PASS: true stale, fresh, clock order, latest pose/device, same-token reconnect, sender rotation\n";
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
