// Offline native POTB -> real Receiver -> real Observation adapter -> fixed
// codec. No PICO runtime/SDK/device or normal named resource is accessed.
#include "pico_ot_backend/observation_adapter.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace b=pico_ot::bridge;namespace p=monaka::protocol::v2;
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
int main(int argc,char**argv)try{
 require(argc==3||argc==4,"usage: pico_p0_adapter_replay IDENTITY OUTPUT [SENDER_SESSION]");
 pico_ot::ObservationIdentity identity(std::filesystem::u8path(argv[1]));
 const auto senderSession=argc==4?std::stoull(argv[3]):101ULL;
 const auto epoch=b::MonotonicNowNs();
 b::Receiver receiver;b::ReceiverConfig rc;rc.listenPort=0;rc.loopbackOnly=true;rc.exclusiveBind=true;rc.rejectRetiredSessions=true;
 b::UdpSocket sender;std::string error;require(receiver.Start(rc,&error),error.c_str());require(sender.Open(0,true,&error,true,true),error.c_str());
 pico_ot::ObservationConfig config;config.sourceId=identity.SourceId();config.spaceId=identity.SpaceId();config.spaceRevision=identity.Revision();
 config.clockEpochPcNs=epoch;config.newUuid=pico_ot::NewObservationUuid;config.sourceSpace=[&](uint64_t session){return identity.RefreshSpace(session);};
 pico_ot::PicoObservationAdapter adapter(config);std::ofstream out(std::filesystem::u8path(argv[2]));require(bool(out),"create new fixture output");
 for(uint32_t i=0;i<90;++i){
  b::PosePacket raw;raw.serial="P0-SYNTHETIC-PICO";raw.connectedSnapshot=true;raw.positionValid=i<40||i>=50;raw.orientationSamplePresent=true;
  raw.positionMeters={1.0+0.001*i,2,3};raw.orientationXyzw={0,0,0,1};raw.linearVelocityMetersPerSec={0.05,0,0};
  std::vector<uint8_t> bytes;require(b::EncodePosePacket(senderSession,i,raw,bytes),"encode native POTB");
  require(sender.SendTo({"127.0.0.1",receiver.ListenPort()},bytes.data(),bytes.size(),&error),error.c_str());
  bool received=false;for(int wait=0;wait<100;++wait){require(receiver.Pump(&error),error.c_str());auto state=receiver.Find(raw.serial);
   if(state&&state->hasPoseSequence&&state->lastPoseSequence==i){received=true;break;}std::this_thread::sleep_for(std::chrono::milliseconds(1));}
  require(received,"synthetic native packet was not received");const auto now=b::MonotonicNowNs();auto values=adapter.Collect(receiver,now);require(values.size()==1,"actual adapter Pose count");
  const auto& observation=std::get<p::TrackerObservation>(values.front());require(observation.validity.position==raw.positionValid,"native loss semantics changed");
  std::string wire;p::Error codec;require(p::EncodeEnvelope(values.front(),wire,codec),codec.message.c_str());
  out<<"{\"received_at_ns\":"<<now<<",\"envelope\":"<<wire<<"}\n";
 }
 require(bool(out),"fixture write failed");std::cerr<<"actual_adapter=PICO source="<<identity.SourceId()<<" device=P0-SYNTHETIC-PICO quality="<<(identity.SpaceKnown()?"known":"unknown")<<" frames=90 physical_hil=NOT_RUN\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}
