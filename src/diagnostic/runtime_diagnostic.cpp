#include "pico_ot_backend/runtime_diagnostic.hpp"
#include "pico_ot_bridge/monotonic_clock.hpp"
#include "pico_diagnostic_build.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace pico_ot {
namespace {
std::string UtcNow() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now); std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    std::ostringstream s; s << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
      << std::setfill('0') << std::setw(3)
      << std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count()%1000 << 'Z';
    return s.str();
}
bool PositionSane(const bridge::Vec3d& p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
bool OrientationSane(const bridge::Quatd& q) {
    if (!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z)||!std::isfinite(q.w)) return false;
    auto n = std::hypot(std::hypot(q.x,q.y),std::hypot(q.z,q.w)); return n>=0.5&&n<=1.5;
}
}
RuntimeDiagnostic::~RuntimeDiagnostic() { Stop(); }
RuntimeDiagnostic::Json RuntimeDiagnostic::Build() { return Json::parse(kPicoDiagnosticBuildJson); }
bool RuntimeDiagnostic::Start(const std::string& path) {
    // Open once before tracking begins. All subsequent output belongs to Worker.
    auto file = std::make_shared<std::ofstream>(path, std::ios::binary|std::ios::app);
    if (!*file) return false;
    StartForTest([file](const std::string& line) { *file << line << '\n'; file->flush();
        if (!*file) throw std::runtime_error("diagnostic sink write failed"); });
    return true;
}
void RuntimeDiagnostic::StartForTest(Writer writer, size_t capacity, size_t reserve) {
    if (Enabled() || !writer || capacity<2 || reserve>=capacity) throw std::invalid_argument("diagnostic configuration");
    writer_=std::move(writer); capacity_=capacity; reserve_=reserve; stopping_=false;
    queue_.resize(capacity_+1); read_=0; write_=0;
    worker_=std::thread([this]{ Worker(); }); enabled_=true;
    Event("component_started", {{"process_boundary","pc_receiver_service"},{"role_available",false}});
    Event("session_connecting", {{"boundary","POTB accepted source packets"}});
}
RuntimeDiagnostic::Json RuntimeDiagnostic::Make(const std::string& type, Json details) {
    Json session={{"session_id", sender_ ? Json(std::to_string(sender_)) : Json(nullptr)},
        {"session_token",sender_ ? Json(std::to_string(sender_)) : Json(nullptr)},
        {"state",state_},{"generation",generation_},{"peer",nullptr}};
    return {{"schema_version",1},{"timestamp_utc",UtcNow()},
        {"monotonic_ns",(std::max)(int64_t{0},bridge::MonotonicNowNs())},
        {"monotonic_domain","windows_qpc"},{"sequence",sequence_++},
        {"component","PicoMotionTrackerBridge"},{"event_type",type},{"source","pico_pc_receiver"},
        {"tracker_identity",nullptr},{"role",nullptr},{"session",session},{"validity",nullptr},
        {"build",Build()},{"details",std::move(details)}};
}
void RuntimeDiagnostic::Event(const std::string& type, Json details, bool critical) noexcept {
    if (!Enabled()) return;
    try { Submit(Make(type,std::move(details)),critical); }
    catch (...) { ++dropped_; if(critical) { ++criticalDropped_; criticalLost_=true; } }
}
void RuntimeDiagnostic::Submit(Json value, bool critical) noexcept {
    try {
        auto write=write_.load(std::memory_order_relaxed);
        auto read=read_.load(std::memory_order_acquire);
        auto used=(write+queue_.size()-read)%queue_.size();
        if (used>=(critical?capacity_:capacity_-reserve_)) {
            ++dropped_; if(critical) { ++criticalDropped_; criticalLost_=true; } return;
        }
        queue_[write]=std::move(value);
        write_.store((write+1)%queue_.size(),std::memory_order_release);
        wake_.notify_one();
    } catch (...) { ++dropped_; if(critical) { ++criticalDropped_; criticalLost_=true; } }
}
void RuntimeDiagnostic::Worker() {
    uint64_t reported=0;
    while (true) {
        Json event;
        auto read=read_.load(std::memory_order_relaxed);
        if(read==write_.load(std::memory_order_acquire)) {
            // Stop may publish terminal events between the empty read and the
            // stopping flag. Re-read after observing Stop before declaring drained.
            if(stopping_.load() && read==write_.load(std::memory_order_acquire)) break;
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait_for(lock,std::chrono::milliseconds(10)); continue;
        }
        event=std::move(queue_[read]);
        read_.store((read+1)%queue_.size(),std::memory_order_release);
        try {
            // Observation timestamps stay immutable. Report only same-domain
            // enqueue/writer lag so HIL does not rejuvenate queued samples.
            event["details"]["diagnostic_delivery_age_ns"]=(std::max)(int64_t{0},
                bridge::MonotonicNowNs()-event["monotonic_ns"].get<int64_t>());
            // Counters travel on every successfully written event, including the final summary.
            event["details"]["dropped_diagnostic_events"]=dropped_.load();
            event["details"]["dropped_events"]=dropped_.load();
            event["details"]["critical_dropped_events"]=criticalDropped_.load();
            event["details"]["observation_complete"]=Complete();
            if(dropped_.load()!=reported) { event["details"]["diagnostic_gap"]=true; reported=dropped_.load(); }
            writer_(event.dump());
        } catch (...) { writeFailed_=true; }
    }
    try { writer_(Json{{"schema_version",1},{"timestamp_utc",UtcNow()},
        {"monotonic_ns",(std::max)(int64_t{0},bridge::MonotonicNowNs())},{"monotonic_domain","windows_qpc"},
        {"component","PicoMotionTrackerBridge"},{"event_type","diagnostic_summary"},
        {"source","pico_pc_receiver"},{"tracker_identity",nullptr},{"role",nullptr},
        {"session",nullptr},{"validity",nullptr},{"build",Build()},
        {"details",{{"diagnostic_delivery_age_ns",0},{"dropped_diagnostic_events",dropped_.load()},{"dropped_events",dropped_.load()},{"critical_dropped_events",criticalDropped_.load()},
          {"observation_complete",Complete()},{"writer_failed",writeFailed_.load()}}}}.dump()); }
    catch (...) { writeFailed_=true; }
}
void RuntimeDiagnostic::TrackerEvent(const std::string& type,const std::string& serial,Tracker& tracker,
    Json validity, Json details, bool critical) {
    auto event=Make(type,std::move(details));
    Json identity={{"domain","hardware_serial"},{"value",serial},{"stable_scope","physical_device"},{"source","POTB.full_serial"}};
    event["tracker_identity"]=identity; event["identities"]=Json::array({identity});
    if(tracker.runtimeId>=0) event["identities"].push_back({{"domain","backend_device_id"},
        {"value",std::to_string(tracker.runtimeId)},{"stable_scope","sender_session"},{"source","POTB.runtimeDeviceId"}});
    event["details"]["role_unavailable_reason"]="PICO backend does not assign body roles";
    event["validity"]=std::move(validity); Submit(std::move(event),critical);
}
void RuntimeDiagnostic::Retire(const std::string& reason) {
    for(auto& item:trackers_) if(item.second.registered) TrackerEvent("tracker_unregistered",item.first,item.second,nullptr,{{"reason",reason}});
    trackers_.clear();
}
void RuntimeDiagnostic::Poll(const bridge::Receiver& receiver,int64_t nowNs) noexcept {
    if(!Enabled()) return;
    try {
        auto sender=receiver.ActiveSenderSessionId();
        int64_t latest=0;
        for(auto& serial:receiver.Serials()) { auto* p=receiver.Find(serial); if(p) latest=(std::max)(latest,(std::max)(p->lastPoseReceivePcNs,p->lastDeviceReceivePcNs)); }
        // A caller can retain a loop timestamp from before Pump admitted this
        // cache. That older observation cannot prove either freshness or a
        // timeout. Leave diagnostic state intact until a usable clock arrives.
        if(nowNs<latest)return;
        bool live=sender!=0 && latest>0 && nowNs>=latest && nowNs-latest<=1'000'000'000LL;
        if (sender!=sender_ || (live&&!connected_)) {
            if(sender_&&connected_) { state_="disconnected"; Event("session_disconnected",{{"reason","sender_session_changed"}}); Retire("session_boundary"); }
            if(sender_ || generation_) { state_="reconnecting"; Event("session_reconnecting"); }
            sender_=sender;
            if(live) { ++generation_; connected_=true; sessionBoundaryNs_=latest; state_="connected"; Event("session_connected",{{"boundary","accepted POTB packet"}}); }
        }
        if(!live&&connected_) {
            connected_=false; state_="disconnected";
            Event("session_disconnected",{{"reason","source_observation_timeout"},{"transport_disconnect_authoritative",false}});
            Retire("source_observation_timeout");
        }
        if(!connected_) return;
        for(auto& serial:receiver.Serials()) {
            auto* p=receiver.Find(serial); if(!p) continue;
            auto& tracker=trackers_[serial];
            auto id=p->hasPoseSequence?p->pose.runtimeDeviceId:(p->deviceState?p->deviceState->runtimeDeviceId:-1);
            if(!tracker.registered) {
                tracker.registered=true; tracker.runtimeId=id;
                TrackerEvent("tracker_discovered",serial,tracker);
                TrackerEvent("tracker_registered",serial,tracker,nullptr,{{"registration_scope","receiver_cache"},{"live_presence_authoritative",false}});
            } else if(id!=tracker.runtimeId) { tracker.runtimeId=id; TrackerEvent("tracker_updated",serial,tracker,nullptr,{{"reason","backend_device_id_changed"}}); }
            if(!p->hasPoseSequence || p->lastPoseReceivePcNs<sessionBoundaryNs_) continue;
            bool fresh=!receiver.IsPoseStale(*p,nowNs);
            // Raw position validity is independently authoritative. Orientation is sample-sanity evidence only.
            bool position=fresh && p->pose.positionValid && PositionSane(p->pose.positionMeters);
            bool orientation=fresh && p->pose.orientationSamplePresent && OrientationSane(p->pose.orientationXyzw);
            Json validity={{"pose_present",true},{"position_valid",position},{"orientation_valid",orientation},{"fully_valid",position&&orientation}};
            Json details={{"position_evidence","POTB.positionValid + finite sample + receive freshness"},
                {"orientation_evidence",orientation?"sample_sanity":"none"},{"orientation_vendor_authoritative",false},
                {"sample_receive_monotonic_ns",p->lastPoseReceivePcNs},{"raw_sequence",p->lastPoseSequence},
                {"source_sensor_liveness_authoritative",false},{"production_modality",position&&orientation?"full":"none"}};
            if(!tracker.poseSeen || tracker.poseSequence!=p->lastPoseSequence) {
                auto first=!tracker.poseSeen; tracker.poseSeen=true; tracker.poseSequence=p->lastPoseSequence;
                TrackerEvent("pose_observed",serial,tracker,validity,details,false);
                if(first) TrackerEvent("first_pose",serial,tracker,validity,details);
            }
            if(!tracker.position || *tracker.position!=position) TrackerEvent("position_valid_changed",serial,tracker,validity,details);
            if(!tracker.orientation || *tracker.orientation!=orientation) TrackerEvent("orientation_valid_changed",serial,tracker,validity,details);
            if(!tracker.full || *tracker.full!=(position&&orientation)) TrackerEvent("fully_valid_changed",serial,tracker,validity,details);
            tracker.position=position; tracker.orientation=orientation; tracker.full=position&&orientation;
            if(position&&!tracker.firstPosition) { tracker.firstPosition=true; TrackerEvent("first_position_valid",serial,tracker,validity,details); }
        }
    } catch (...) { ++dropped_; ++criticalDropped_; criticalLost_=true; }
}
void RuntimeDiagnostic::Stop() {
    if(!Enabled()) return;
    Event("component_stopping"); Retire("component_stop"); state_="disconnected";
    if(connected_) Event("session_disconnected",{{"reason","component_stop"}});
    Event("component_stopped"); enabled_=false;
    stopping_=true; wake_.notify_all();
    if(worker_.joinable()) worker_.join();
    writer_={}; // Close the JSONL stream after the drained terminal summary.
}
}
