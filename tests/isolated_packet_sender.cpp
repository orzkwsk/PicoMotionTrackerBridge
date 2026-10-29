#include <winsock2.h>
#include "pico_ot_bridge/isolated_resources.hpp"
#include "pico_ot_bridge/protocol.hpp"
#include "pico_ot_bridge/udp_socket.hpp"
#include <iostream>
int main(int argc,char** argv) try {
    if(argc!=8)throw std::runtime_error("TEST ONLY: PROFILE SERIAL SESSION SEQUENCE full|none|malformed RECEIVER_PID OUTPUT_PACKET_FILE");
    const auto profile=pico_ot::bridge::IsolationProfile::Load(std::filesystem::u8path(argv[1]));profile.ValidateOwner();
    const auto receiverPid=std::stoul(argv[6]);
    ULONG size=0;GetExtendedUdpTable(nullptr,&size,FALSE,AF_INET,UDP_TABLE_OWNER_PID,0);std::vector<unsigned char> table(size);
    if(GetExtendedUdpTable(table.data(),&size,FALSE,AF_INET,UDP_TABLE_OWNER_PID,0)!=NO_ERROR)throw std::runtime_error("receiver owner census failed");
    const auto* rows=reinterpret_cast<const MIB_UDPTABLE_OWNER_PID*>(table.data());bool found=false;
    for(DWORD i=0;i<rows->dwNumEntries;++i)if(rows->table[i].dwLocalAddr==htonl(INADDR_LOOPBACK)&&ntohs(static_cast<u_short>(rows->table[i].dwLocalPort))==profile.ingress&&rows->table[i].dwOwningPid==receiverPid)found=true;
    if(!found)throw std::runtime_error("target ingress not owned by test receiver");
    const std::string serial=argv[2],mode=argv[5];if(serial.rfind("R5-FAKE-",0)!=0)throw std::runtime_error("fake origin marker required");
    std::vector<uint8_t> packet;
    pico_ot::bridge::PosePacket pose;pose.serial=serial;pose.connectedSnapshot=true;pose.positionValid=mode=="full";pose.orientationSamplePresent=mode=="full";pose.orientationXyzw={0,0,0,1};pose.positionMeters={1,2,3};
    if(mode=="malformed")packet={1,2,3};
    else if(mode!="full"&&mode!="none")throw std::runtime_error("unknown mode");
    else if(!pico_ot::bridge::EncodePosePacket(std::stoull(argv[3]),static_cast<uint32_t>(std::stoul(argv[4])),pose,packet))throw std::runtime_error("packet encoding failed");
    // The harness retains the sending socket for the whole service lifetime.
    // Emit product-encoded fixture bytes; do not create a short-lived UDP peer.
    std::ofstream output(std::filesystem::u8path(argv[7]),std::ios::binary);
    output.write(reinterpret_cast<const char*>(packet.data()),packet.size());output.flush();
    if(!output)throw std::runtime_error("cannot persist fake product packet");
    std::cout<<"FAKE POTB "<<mode<<" serial="<<serial<<" session="<<argv[3]<<" sequence="<<argv[4]<<"\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
