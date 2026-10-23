// One-time migration from the old hidden-console service. Never force-kill.
// Caller separately pins the executable hash. PID/creation/path and exclusive
// console membership are revalidated here immediately before CTRL_C.
#include "pico_ot_bridge/service_lifecycle.hpp"
#include <iostream>

BOOL WINAPI IgnoreOwnSignal(DWORD){return TRUE;}
int wmain(int argc,wchar_t** argv)try{
    if(argc!=4)return 2;
    const auto raw=std::stoull(argv[1]);const auto created=std::stoull(argv[2]);
    if(!raw||raw>0xffffffffULL||!created)return 2;
    const auto pid=static_cast<DWORD>(raw);
    pc_service_lifecycle::Handle process(pc_service_lifecycle::OpenIdentity(pid,created));
    wchar_t path[32768];DWORD size=32768;
    if(!QueryFullProcessImageNameW(process.value,0,path,&size)||_wcsicmp(path,argv[3])!=0)return 3;
    FreeConsole();
    if(!AttachConsole(pid))return 4;
    DWORD members[16];const auto count=GetConsoleProcessList(members,16);
    bool target=false,self=false;
    for(DWORD i=0;i<count&&i<16;++i){target|=members[i]==pid;self|=members[i]==GetCurrentProcessId();}
    if(count!=2||!target||!self){FreeConsole();return 5;}
    if(!SetConsoleCtrlHandler(IgnoreOwnSignal,TRUE)||!GenerateConsoleCtrlEvent(CTRL_C_EVENT,0)){FreeConsole();return 6;}
    if(WaitForSingleObject(process.value,5000)!=WAIT_OBJECT_0)return 124;
    DWORD exit=1;if(!GetExitCodeProcess(process.value,&exit)||exit!=0)return 7;
    std::cout<<"Owned console service exited normally\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 8;}
