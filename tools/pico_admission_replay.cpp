#include "pico_ot_backend/admission_diagnostic.hpp"
#include <array>
#include <iostream>
#include <map>

namespace p=monaka::protocol::v2;
using J=nlohmann::json;
struct Device {
    pico_ot::PicoReacquireAdmission gate;
    std::string binding;
    std::optional<int64_t> sequence;
    Device(pico_ot::AdmissionConfig config,std::string key):gate(config),binding(std::move(key)){}
};
int main() {
    try {
        pico_ot::AdmissionConfig config;
        std::map<std::string,Device> devices;
        bool started=false;
        std::array<char,65536> line{};
        while (std::cin.getline(line.data(),static_cast<std::streamsize>(line.size()))) {
            const auto j=J::parse(line.data());
            if (j.contains("config")) {
                if (started) throw std::invalid_argument("config must precede samples");
                config=pico_ot::ParseAdmissionConfig(j.at("config"));
                std::cout<<J{{"config",pico_ot::AdmissionConfigJson(config)}}.dump()<<'\n';
                continue;
            }
            started=true;
            const auto t=j.at("timestamp_ns").get<int64_t>();
            const auto sequence=j.at("sequence").get<int64_t>();
            if (t<0 || sequence<0) throw std::invalid_argument("negative replay timestamp/sequence");
            p::TrackerObservation v;
            v.version={2,0}; v.source_id=j.value("source_id","pico-offline");
            v.device_id=j.value("device_id","pico-offline-tracker");
            v.session_id=j.value("session_id","00000000-0000-4000-8000-000000000001");
            v.clock_id=j.value("clock_id","00000000-0000-4000-8000-000000000002");
            v.coordinate_space={"pico-offline-map","pico-output-a-v1",7};
            if (j.contains("coordinate_space")) {
                const auto& s=j.at("coordinate_space");
                v.coordinate_space={s.at("id").get<std::string>(),s.at("convention").get<std::string>(),s.at("revision").get<uint32_t>()};
            }
            v.timestamp_ns=t; v.sent_at_ns=t; v.sequence=sequence; v.timestamp_kind="receive";
            v.capabilities={"position","orientation","linear_velocity"};
            const auto key=J::array({v.source_id,v.device_id}).dump();
            const auto binding=J::array({v.session_id,v.clock_id,v.coordinate_space.id,v.coordinate_space.convention,v.coordinate_space.revision}).dump();
            if (!devices.count(key)) {
                if (devices.size()>=256) throw std::invalid_argument("replay device bound exceeded");
                devices.try_emplace(key,config,binding);
            }
            auto& device=devices.at(key);
            if (device.binding!=binding) device=Device(config,binding);
            if (device.sequence && sequence<=*device.sequence) {
                std::cout<<J{{"dropped",true},{"reason","duplicate_or_reordered_sequence"},{"sequence",std::to_string(sequence)}}.dump()<<'\n';
                continue;
            }
            device.sequence=sequence;
            pico_ot::bridge::PosePacket raw;
            raw.serial=v.device_id; raw.positionValid=j.at("raw_valid").get<bool>();
            raw.orientationSamplePresent=j.value("raw_orientation_present",raw.positionValid);
            const auto pos=j.at("position").get<std::array<double,3>>();
            const auto q=j.at("quaternion").get<std::array<double,4>>();
            raw.positionMeters={pos[0],pos[1],pos[2]}; raw.orientationXyzw={q[0],q[1],q[2],q[3]};
            raw.rawTrackingState65=j.value("native_tracking_state",uint8_t{0});
            raw.sourceTimestamp=j.value("source_timestamp",uint64_t{0});
            if (j.contains("linear_velocity")) { const auto a=j.at("linear_velocity").get<std::array<double,3>>();raw.linearVelocityMetersPerSec={a[0],a[1],a[2]}; }
            if (j.contains("angular_velocity")) { const auto a=j.at("angular_velocity").get<std::array<double,3>>();raw.angularVelocityRadPerSec={a[0],a[1],a[2]}; }
            const auto r=pico_ot::ApplyPicoAdmission(v,raw,t,j.value("fresh",true),device.gate);
            std::string wire; p::Error error;
            if (!p::EncodeEnvelope(v,wire,error)) throw std::runtime_error(error.message);
            auto out=pico_ot::AdmissionDiagnosticJson(raw,r,v);
            out["protocol"]=J::parse(wire);
            std::cout<<out.dump()<<'\n';
        }
        if (!std::cin.eof()) throw std::invalid_argument("replay line exceeds 65535 bytes or input failed");
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n';return 1; }
}
