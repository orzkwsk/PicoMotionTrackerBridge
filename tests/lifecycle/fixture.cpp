#include "pico_ot_bridge/service_lifecycle.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>
#include <memory>
std::atomic<bool> running{true};
BOOL WINAPI StopFixture(DWORD type){if(type==CTRL_C_EVENT){running=false;return TRUE;}return FALSE;}

int main(int argc,char** argv) try {
    if(argc<2)return 2;
    int first=1;
    if(std::string(argv[1])=="--isolated-profile") {if(argc<3)return 2;first=2;}
    std::string scope=argv[first];
    if(scope.rfind("Local\\PicoOTBridge_Test_",0)!=0)return 3;
    bool ignore=false,fail=false,stopError=false;
    unsigned delay=0,drop=0;
    DWORD owner=0;std::uint64_t created=0;
    for(int i=first+1;i<argc;++i) {
        std::string option=argv[i];
        if(option=="--tray-owner" && i+1<argc)pc_service_lifecycle::ParseOwner(argv[++i],owner,created);
        else if(option=="--ignore-stop")ignore=true;
        else if(option=="--exit-error")fail=true;
        else if(option=="--stop-error")stopError=true;
        else if(option=="--ready-delay" && i+1<argc)delay=std::stoul(argv[++i]);
        else if(option=="--drop-map" && i+1<argc)drop=std::stoul(argv[++i]);
        else return 4;
    }
    if(fail) {std::cerr<<"fixture startup failure\n";return 17;}
    SetConsoleCtrlHandler(StopFixture,TRUE);
    auto endpoint=std::make_unique<pc_service_lifecycle::Endpoint>(std::wstring(scope.begin(),scope.end()),owner,created);
    if(delay)std::this_thread::sleep_for(std::chrono::milliseconds(delay));
    endpoint->MarkReady();
    std::cout<<"READY\n"<<std::flush;
    const auto start=GetTickCount64();
    while(GetTickCount64()-start<(ignore?1500ULL:12000ULL)) {
        if(!running)return 0;
        if(endpoint && endpoint->StopRequested() && !ignore)return stopError?17:0;
        if(drop && GetTickCount64()-start>=drop)endpoint.reset();
        if(drop && GetTickCount64()-start>=1500)return 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ignore?0:9; // bounded fixture; never kill a process on test failure.
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 10;}
