#include <winsock2.h>
#include "pico_ot_bridge/isolated_resources.hpp"
#include "pico_ot_bridge/receiver.hpp"
#include "pico_ot_backend/observation_identity.hpp"
#include <iostream>
namespace fs=std::filesystem;
using namespace pico_ot::bridge;
void check(bool value,const char* what) {if(!value)throw std::runtime_error(what);}
template<class F> void rejects(F operation,const char* what) {bool failed=false;try{operation();}catch(const std::exception&){failed=true;}check(failed,what);}
int main(int argc,char** argv) try {
    check(argc==2,"fresh test parent required");
    const auto id=pico_ot::NewObservationUuid();
    const auto root=fs::absolute(argv[1])/id;check(fs::create_directories(root),"fresh root");
    UdpSocket sockets[5];std::string error;
    for(auto& socket:sockets)check(socket.Open(0,true,&error,true,true),error.c_str());
    auto value=nlohmann::json{{"version",1},{"instance_id",id},{"state_root",root.u8string()},
        {"owner_pid",GetCurrentProcessId()},{"owner_created",std::to_string(pc_service_lifecycle::Creation(GetCurrentProcess()))},
        {"ingress_port",sockets[0].BoundPort()},{"steamvr_port",sockets[1].BoundPort()},{"monaka_port",sockets[2].BoundPort()},
        {"utility_port",sockets[3].BoundPort()},{"observation_port",sockets[4].BoundPort()}};
    const auto profile=root.parent_path()/(id+".json");
    const auto save=[&](const nlohmann::json& bytes){std::ofstream(profile)<<bytes.dump();};
    save(value);auto p=IsolationProfile::Load(profile);
    const auto leasePath=root/L"owner.claim";
    pc_service_lifecycle::Handle lease(CreateFileW(leasePath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr));
    check(lease.value!=INVALID_HANDLE_VALUE,"new retained lease");const auto record=p.OwnerRecord();DWORD written=0;
    check(WriteFile(lease.value,record.data(),static_cast<DWORD>(record.size()),&written,nullptr)&&written==record.size()&&FlushFileBuffers(lease.value),"lease write");
    p.ValidateOwner();
    // The read/probe must not manufacture its own sharing violation.
    CloseHandle(lease.value);lease.value=nullptr;
    rejects([&]{p.ValidateOwner();},"released lease accepted");
    lease.value=CreateFileW(leasePath.c_str(),GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    check(lease.value!=INVALID_HANDLE_VALUE,"retain lease again");p.ValidateOwner();
    sockets[1].Close();rejects([&]{p.ValidateOwner();},"missing output listener accepted");
    for(const auto* key:{"version","instance_id","state_root","owner_pid","owner_created","ingress_port","steamvr_port","monaka_port","utility_port","observation_port"}){
        auto missing=value;missing.erase(key);save(missing);rejects([&]{IsolationProfile::Load(profile);},"partial profile accepted");
    }
    auto invalid=value;invalid["ingress_port"]=29765;save(invalid);rejects([&]{IsolationProfile::Load(profile);},"production port accepted");
    invalid=value;invalid["utility_port"]=value["monaka_port"];save(invalid);rejects([&]{IsolationProfile::Load(profile);},"duplicate port accepted");
    invalid=value;invalid["extra"]=true;save(invalid);rejects([&]{IsolationProfile::Load(profile);},"unknown key accepted");
    invalid=value;invalid["ingress_port"]="40000";save(invalid);rejects([&]{IsolationProfile::Load(profile);},"string port accepted");
    std::ofstream(profile)<<"{\"version\":1,\"version\":1}";rejects([&]{IsolationProfile::Load(profile);},"duplicate key accepted");
    save(value);
    Receiver receiver;ReceiverConfig config;config.listenPort=sockets[0].BoundPort();config.exclusiveBind=true;config.loopbackOnly=true;
    check(!receiver.Start(config,&error),"occupied exclusive ingress accepted");
    sockets[0].Close();check(receiver.Start(config,&error),error.c_str());receiver.Stop();
    std::cout<<"PASS complete profile; missing/unknown/duplicate/reserved/type negatives; exact listeners; retained/released lease; exclusive loopback collision/cleanup\n";
    return 0;
} catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
