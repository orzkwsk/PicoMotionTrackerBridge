#include "pico_ot_backend/admission_diagnostic.hpp"
#include "require.hpp"
#include <cmath>
#include <iostream>
#include <limits>

using namespace pico_ot;
bridge::PosePacket Pose() { bridge::PosePacket p; p.positionValid=true;p.orientationSamplePresent=true;return p; }
void StateMachine() {
    PicoReacquireAdmission g({true,20,3});auto p=Pose();
    REQUIRE(g.Process(p,0,false).state==AdmissionState::Invalid);
    auto r=g.Process(p,1'000'000,true);REQUIRE(r.state==AdmissionState::Candidate && r.candidate_samples==1);
    r=g.Process(p,21'000'000,true);REQUIRE(r.state==AdmissionState::Candidate); // time only
    r=g.Process(p,21'000'000,true);REQUIRE(r.candidate_samples==2 && !r.effective_valid);
    r=g.Process(p,22'000'000,true);REQUIRE(r.effective_valid);
    REQUIRE(g.Process(p,22'000'000,false).state==AdmissionState::Invalid); // immediate loss even duplicate
    r=g.Process(p,23'000'000,true);REQUIRE(r.candidate_elapsed_ns==0 && r.candidate_samples==1);
    REQUIRE(g.Process(p,24'000'000,false).state==AdmissionState::Invalid);
    r=g.Process(p,25'000'000,true);REQUIRE(r.candidate_samples==1 && r.candidate_elapsed_ns==0);
    r=g.Process(p,26'000'000,true);REQUIRE(!r.effective_valid);
    r=g.Process(p,27'000'000,true);REQUIRE(!r.effective_valid); // samples only
    REQUIRE(g.Process(p,45'000'000,true).effective_valid);
    r=g.Process(p,600'000'000,true);REQUIRE(r.state==AdmissionState::Candidate && r.candidate_samples==1);
    r=g.Process(p,500'000'000,true);REQUIRE(r.state==AdmissionState::Invalid);
    REQUIRE(!g.Process(p,600'000'000,true).effective_valid);
    REQUIRE(g.Process(p,601'000'000,true).candidate_samples==1);
    REQUIRE(!g.Process(p,602'000'000,true,false).effective_valid);
    PicoReacquireAdmission zero({true,0,0});REQUIRE(zero.Process(p,0,true).effective_valid);
    PicoReacquireAdmission off;REQUIRE(off.Process(p,0,true).effective_valid);
    REQUIRE(!off.Process(p,0,false).effective_valid);
}
void MetricsAndConfig() {
    auto p=Pose();PicoReacquireAdmission g({true,20,3});g.Process(p,0,true);
    p.orientationXyzw.w=-1;auto r=g.Process(p,1'000'000,true);
    REQUIRE(r.quaternion_step_rad && std::abs(*r.quaternion_step_rad)<1e-10);
    r=g.Process(p,1'000'000,true);REQUIRE(!r.linear_speed_mps && !r.angular_speed_radps);
    p.positionMeters.x=(std::numeric_limits<double>::infinity)();p.orientationXyzw.w=0;
    r=g.Process(p,2'000'000,false);REQUIRE(!r.position_step_m && !r.quaternion_step_rad);
    for (const auto& invalid:{nlohmann::json{{"enabled",1}},nlohmann::json{{"reacquire_confirm_ms",-1}},
        nlohmann::json{{"reacquire_min_samples",0.5}},nlohmann::json{{"other",0}},
        nlohmann::json{{"reacquire_confirm_ms",(std::numeric_limits<int64_t>::max)()}}}) {
        bool caught=false;try { ParseAdmissionConfig(invalid); }catch(const std::exception&) { caught=true; }REQUIRE(caught);
    }
    REQUIRE(!ParseAdmissionConfig(nlohmann::json::object()).enabled);
}
int main(){StateMachine();MetricsAndConfig();std::cout<<"PASS admission transitions, conjunction, resets, timing, metrics, config\n";}
