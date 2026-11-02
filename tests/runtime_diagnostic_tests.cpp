#include "pico_ot_backend/runtime_diagnostic.hpp"
#include "pico_ot_bridge/publisher.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
#include "require.hpp"
#include <chrono>
#include <condition_variable>
#include <ctime>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <mutex>
#include <thread>
#ifdef _WIN32
#include <Windows.h>
#include <Psapi.h>
#endif

using pico_ot::RuntimeDiagnostic;
using Json=RuntimeDiagnostic::Json;
using namespace pico_ot::bridge;
namespace {
std::string fixturePath;
pico_ot::TrackerState Pose(bool position=true,bool orientation=true) {
    pico_ot::TrackerState p; p.serial="SYNTHETIC-PICO-A"; p.runtimeDeviceId=4;
    p.positionValid=position; p.orientationSamplePresent=orientation;
    p.orientationXyzw={0,0,0,1}; p.positionMeters={1,2,3}; return p;
}
void Pump(Receiver& r) {
    std::this_thread::sleep_for(std::chrono::milliseconds(3));
    REQUIRE(r.Pump()); std::this_thread::sleep_for(std::chrono::milliseconds(3));
}
void Lifecycle() {
    RuntimeDiagnostic d; Receiver r; ReceiverConfig rc; rc.listenPort=0; rc.staleTimeoutNs=500'000'000;
    REQUIRE(r.Start(rc)); std::vector<Json> events;
    d.StartForTest([&](const std::string& s){events.push_back(Json::parse(s));});
    r.SetDiagnosticObserver([&]{d.Poll(r,MonotonicNowNs());});
    d.Event("component_ready");
    Publisher p; PublisherConfig pc; pc.pcEndpoint={"127.0.0.1",r.ListenPort()}; pc.senderSessionId=101;
    REQUIRE(p.Start(pc)); REQUIRE(p.SendPose(Pose(false,false))); Pump(r);
    REQUIRE(p.SendPose(Pose(true,false))); Pump(r);
    REQUIRE(p.SendPose(Pose(true,true))); Pump(r);
    // Same Pump must observe both intermediate validity transitions.
    REQUIRE(p.SendPose(Pose(false,true))); REQUIRE(p.SendPose(Pose(true,true))); Pump(r);
    auto* state=r.Find("SYNTHETIC-PICO-A"); REQUIRE(state);
    d.Poll(r,state->lastPoseReceivePcNs+600'000'000); // freshness transition independently of session
    d.Poll(r,state->lastPoseReceivePcNs+1'100'000'000); // source observation timeout
    REQUIRE(p.SendPose(Pose(true,true))); Pump(r); // reconnect with same sender token
    p.Stop(); pc.senderSessionId=102; REQUIRE(p.Start(pc)); REQUIRE(p.SendPose(Pose())); Pump(r);
    d.Event("fatal_error",{{"synthetic",true}});
    d.Event("future_custom_event",{{"synthetic",true}}); d.Stop(); r.Stop(); p.Stop();
    std::unordered_map<std::string,size_t> counts; uint64_t last=0; bool first=true;
    for(auto& e:events) {
        ++counts[e["event_type"].get<std::string>()];
        REQUIRE(e["schema_version"]==1); REQUIRE(e["role"].is_null());
        REQUIRE(e.contains("timestamp_utc")&&e.contains("validity")&&e.contains("session"));
        REQUIRE(e["monotonic_ns"].is_number_integer()); auto now=e["monotonic_ns"].get<uint64_t>();
        REQUIRE(e["details"]["diagnostic_delivery_age_ns"].is_number_integer());
        REQUIRE(e["details"]["diagnostic_delivery_age_ns"].get<int64_t>()>=0);
        REQUIRE(first||now>=last); first=false; last=now;
        if(!e["tracker_identity"].is_null()) {
            REQUIRE(e["tracker_identity"]["domain"]=="hardware_serial");
            REQUIRE(e["tracker_identity"]["value"]=="SYNTHETIC-PICO-A");
            REQUIRE(e["identities"][1]["domain"]=="backend_device_id");
        }
    }
    REQUIRE(counts["component_started"]==1); REQUIRE(counts["component_ready"]==1);
    REQUIRE(counts["session_connected"]==3); REQUIRE(counts["session_reconnecting"]==2);
    REQUIRE(counts["first_pose"]==3); REQUIRE(counts["first_position_valid"]==3);
    REQUIRE(counts["tracker_registered"]==3); REQUIRE(counts["tracker_unregistered"]==3);
    REQUIRE(counts["position_valid_changed"]>=7); REQUIRE(counts["orientation_valid_changed"]>=5);
    REQUIRE(counts["fatal_error"]==1); REQUIRE(counts["future_custom_event"]==1);
    REQUIRE(counts["component_stopped"]==1); REQUIRE(counts["diagnostic_summary"]==1);
    auto build=RuntimeDiagnostic::Build(); REQUIRE(build["git_commit"].get<std::string>().size()==40);
    REQUIRE(build["dirty_at_build"].is_boolean()); REQUIRE(build["binary_sha256"].is_null());
    if(!fixturePath.empty()) { std::ofstream output(fixturePath); for(auto& event:events) output<<event.dump()<<'\n'; }
    std::cout << "lifecycle_event_count=" << events.size() << " critical_dropped=" << events.back()["details"]["critical_dropped_events"] << '\n';
}
void DeferredLoopClock() {
    RuntimeDiagnostic d; Receiver r; ReceiverConfig rc;
    rc.listenPort=0; rc.staleTimeoutNs=500'000'000;
    REQUIRE(r.Start(rc)); std::vector<Json> events;
    d.StartForTest([&](const std::string& s){events.push_back(Json::parse(s));});
    r.SetDiagnosticObserver([&]{d.Poll(r,MonotonicNowNs());});
    d.Poll(r,MonotonicNowNs()); // No accepted source yet.
    Publisher p; PublisherConfig pc;
    pc.pcEndpoint={"127.0.0.1",r.ListenPort()}; pc.senderSessionId=301;
    REQUIRE(p.Start(pc));
    for(int i=0;i<32;++i) {
        // The service used to capture this time before Pump, whose callback
        // observes a newer receipt, then pass the older time to the idle poll.
        const auto loopNow=MonotonicNowNs();
        REQUIRE(p.SendPose(Pose())); Pump(r);
        auto* state=r.Find("SYNTHETIC-PICO-A"); REQUIRE(state);
        const auto received=state->lastPoseReceivePcNs;
        const auto sequence=state->lastPoseSequence;
        REQUIRE(loopNow<received);
        for(int repeat=0;repeat<3;++repeat)d.Poll(r,loopNow);
        d.Poll(r,MonotonicNowNs());
        REQUIRE(state->lastPoseReceivePcNs==received&&state->lastPoseSequence==sequence);
        REQUIRE(state->pose.positionMeters.x==1&&state->pose.orientationXyzw.w==1);
    }
    auto* state=r.Find("SYNTHETIC-PICO-A"); REQUIRE(state);
    const auto received=state->lastPoseReceivePcNs;
    d.Event("synthetic_checkpoint",{{"name","continuous"}});
    d.Poll(r,received+500'000'000); // Inclusive pose freshness boundary.
    d.Event("synthetic_checkpoint",{{"name","pose_boundary"}});
    d.Poll(r,received+500'000'001); // Pose expires independently of session.
    d.Event("synthetic_checkpoint",{{"name","pose_expired"}});
    d.Poll(r,received+1'000'000'000); // Inclusive session liveness boundary.
    d.Event("synthetic_checkpoint",{{"name","session_boundary"}});
    d.Poll(r,received+1'000'000'001); // Real source-observation timeout.
    d.Event("synthetic_checkpoint",{{"name","session_expired"}});
    d.Poll(r,received+1'000'000'002); // No duplicate disconnect during silence.
    REQUIRE(p.SendPose(Pose())); Pump(r); // Same-token reconnect still resets.
    d.Stop(); r.Stop(); p.Stop();
    std::unordered_map<std::string,size_t> counts;
    size_t timeouts=0,componentStops=0;
    std::unordered_map<std::string,Json> checkpoints;
    bool position=false;
    for(const auto& event:events) {
        ++counts[event["event_type"].get<std::string>()];
        if(event["event_type"]=="position_valid_changed")position=event["validity"]["position_valid"];
        if(event["event_type"]=="synthetic_checkpoint")
            checkpoints[event["details"]["name"].get<std::string>()]=Json{
                {"position",position},{"connected",counts["session_connected"]},
                {"timeout",timeouts},{"observed",counts["pose_observed"]}};
        if(event["event_type"]=="session_disconnected") {
            const auto reason=event["details"].value("reason",std::string());
            if(reason=="source_observation_timeout") {
                ++timeouts; REQUIRE(event["details"]["transport_disconnect_authoritative"]==false);
            }
            if(reason=="component_stop")++componentStops;
        }
    }
    if(!fixturePath.empty()) {
        std::ofstream output(fixturePath+".deferred-clock.jsonl");
        for(const auto& event:events)output<<event.dump()<<'\n';
    }
    std::cout<<"deferred_clock connected="<<counts["session_connected"]
        <<" timeout="<<timeouts<<" first_pose="<<counts["first_pose"]
        <<" observed="<<counts["pose_observed"]<<'\n';
    REQUIRE(checkpoints["continuous"]==Json({{"position",true},{"connected",1},{"timeout",0},{"observed",32}}));
    REQUIRE(checkpoints["pose_boundary"]["position"]==true);
    REQUIRE(checkpoints["pose_expired"]["position"]==false&&checkpoints["pose_expired"]["timeout"]==0);
    REQUIRE(checkpoints["session_boundary"]["timeout"]==0);
    REQUIRE(checkpoints["session_expired"]["timeout"]==1);
    REQUIRE(timeouts==1&&componentStops==1);
    REQUIRE(counts["session_connected"]==2&&counts["session_reconnecting"]==1);
    REQUIRE(counts["tracker_registered"]==2&&counts["tracker_unregistered"]==2);
    REQUIRE(counts["first_pose"]==2&&counts["first_position_valid"]==2);
    REQUIRE(counts["pose_observed"]==33);
    REQUIRE(counts["position_valid_changed"]==3&&counts["orientation_valid_changed"]==3);
    REQUIRE(d.Complete()&&d.Dropped()==0);
}
void Backpressure() {
    std::mutex m; std::condition_variable cv; bool release=false,entered=false; std::vector<Json> events;
    RuntimeDiagnostic d;
    d.StartForTest([&](const std::string& s) {
        std::unique_lock<std::mutex> lock(m); entered=true; cv.notify_all(); cv.wait(lock,[&]{return release;});
        events.push_back(Json::parse(s));
    },8,4);
    {std::unique_lock<std::mutex> lock(m); cv.wait(lock,[&]{return entered;});}
    for(int i=0;i<100;++i) d.Event("pose_observed",Json::object(),false);
    d.Event("fatal_error",{{"synthetic",true}}); // reserved capacity retains fatal transition
    for(int i=0;i<100;++i) d.Event("session_connected"); // critical overflow fails completeness
    REQUIRE(d.Dropped()>0); REQUIRE(!d.Complete());
    {std::lock_guard<std::mutex> lock(m); release=true;} cv.notify_all(); d.Stop();
    bool fatal=false; for(auto& e:events) fatal|=e["event_type"]=="fatal_error";
    REQUIRE(fatal); REQUIRE(events.back()["event_type"]=="diagnostic_summary");
    REQUIRE(events.back()["details"]["observation_complete"]==false);
    REQUIRE(events.back()["details"]["critical_dropped_events"].get<int>()>0);
    std::cout << "backpressure_dropped=" << d.Dropped() << '\n';
    RuntimeDiagnostic broken; broken.StartForTest([](const std::string&){throw std::runtime_error("synthetic sink failure");});
    broken.Event("fatal_error"); broken.Stop(); REQUIRE(!broken.Complete());
}
void ImmediateStopDrain() {
    for(int attempt=0;attempt<100;++attempt) {
        size_t count=0; bool stopped=false,summary=false;
        RuntimeDiagnostic d;
        d.StartForTest([&](const std::string& s) {
            auto event=Json::parse(s); ++count;
            if(event["event_type"]=="component_stopped") stopped=true;
            if(event["event_type"]=="diagnostic_summary") { REQUIRE(stopped); summary=true; }
        });
        for(int i=0;i<20;++i) d.Event("diagnostic_warning",{{"synthetic",true}});
        d.Stop(); REQUIRE(count==25); REQUIRE(stopped&&summary); REQUIRE(d.Complete());
    }
}
void Smoke() {
    auto measure=[](const char* name,bool on,bool active) {
        Receiver r; ReceiverConfig rc; rc.listenPort=0; REQUIRE(r.Start(rc));
        RuntimeDiagnostic d; size_t written=0; int64_t writeTotalNs=0,writeMaxNs=0;
        auto outputDir=fixturePath.empty()?std::filesystem::current_path():std::filesystem::path(fixturePath).parent_path();
        std::ofstream output(outputDir/(std::string("diagnostic-smoke-")+name+".jsonl"),std::ios::binary);
        if(on)d.StartForTest([&](const std::string& line){
            auto start=std::chrono::steady_clock::now(); output<<line<<'\n'; output.flush();
            auto elapsed=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();
            writeTotalNs+=elapsed;writeMaxNs=(std::max)(writeMaxNs,elapsed);++written;
        });
        Publisher p; PublisherConfig pc; pc.pcEndpoint={"127.0.0.1",r.ListenPort()}; pc.senderSessionId=200;
        if(active) { REQUIRE(p.Start(pc)); if(on)r.SetDiagnosticObserver([&]{d.Poll(r,MonotonicNowNs());}); }
        auto begin=std::chrono::steady_clock::now(); auto cpu=std::clock();
        const int n=active?5000:50000;
        for(int i=0;i<n;++i) { if(active){REQUIRE(p.SendPose(Pose()));REQUIRE(r.Pump());} d.Poll(r,MonotonicNowNs()); }
        auto loop=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-begin).count();
        auto cpuMs=1000.0*(std::clock()-cpu)/CLOCKS_PER_SEC; d.Stop();
        size_t memory=0;
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS pmc{}; if(GetProcessMemoryInfo(GetCurrentProcess(),&pmc,sizeof(pmc)))memory=pmc.WorkingSetSize;
#endif
        std::cout<<"smoke "<<name<<" iterations="<<n<<" loop_us="<<loop<<" cpu_ms="<<cpuMs
          <<" working_set_bytes="<<memory<<" written="<<written<<" dropped="<<d.Dropped()<<" complete="<<d.Complete()<<'\n';
        std::cout<<"write_latency "<<name<<" average_ns="<<(written?writeTotalNs/static_cast<int64_t>(written):0)
          <<" max_ns="<<writeMaxNs<<" event_rate_per_second="<<(loop?1000000.0*written/loop:0)<<'\n';
    };
    measure("off_idle",false,false); measure("on_idle",true,false);
    measure("off_active",false,true); measure("on_active",true,true);
}
}
int main(int argc,char** argv){
    try { if(argc>1)fixturePath=argv[1]; Lifecycle();DeferredLoopClock();Backpressure();ImmediateStopDrain();Smoke();std::cout<<"runtime diagnostic tests passed\n"; }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
