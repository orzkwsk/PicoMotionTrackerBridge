#include "pico_ot_bridge/service_lifecycle.hpp"
#include <chrono>
#include <iostream>
#include <thread>
#include <atomic>
std::atomic<bool> running{true};
BOOL WINAPI StopFixture(DWORD type){if(type==CTRL_C_EVENT){running=false;return TRUE;}return FALSE;}

int main(int argc,char** argv) try {
    if(argc<2)return 2;
    std::string scope=argv[1];
    if(scope.rfind("Local\\PicoOTBridge_Test_",0)!=0)return 3;
    bool ignore=false,fail=false,stopError=false;
    DWORD owner=0;std::uint64_t created=0;
    for(int i=2;i<argc;++i) {
        std::string option=argv[i];
        if(option=="--tray-owner" && i+1<argc)pc_service_lifecycle::ParseOwner(argv[++i],owner,created);
        else if(option=="--ignore-stop")ignore=true;
        else if(option=="--exit-error")fail=true;
        else if(option=="--stop-error")stopError=true;
        else return 4;
    }
    if(fail) {std::cerr<<"fixture startup failure\n";return 17;}
    SetConsoleCtrlHandler(StopFixture,TRUE);
    pc_service_lifecycle::Endpoint endpoint(std::wstring(scope.begin(),scope.end()),owner,created);
    endpoint.MarkReady();
    std::cout<<"READY\n"<<std::flush;
    const auto start=GetTickCount64();
    while(GetTickCount64()-start<(ignore?1500ULL:12000ULL)) {
        if(!running)return 0;
        if(endpoint.StopRequested() && !ignore)return stopError?17:0;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ignore?0:9; // bounded fixture; never kill a process on test failure.
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 10;}
