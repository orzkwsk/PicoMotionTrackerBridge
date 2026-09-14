#include "pico_ot_backend/observation_identity.hpp"
#include "monaka/protocol/v1/codec.hpp"
#include <array>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace pico_ot {
std::string NewObservationUuid() {
    std::random_device random;
    std::array<unsigned char,16> bytes{};
    for (auto& byte : bytes) byte = static_cast<unsigned char>(random());
    bytes[6] = static_cast<unsigned char>((bytes[6]&15)|64);
    bytes[8] = static_cast<unsigned char>((bytes[8]&63)|128);
    const char* hex = "0123456789abcdef"; std::string result;
    for (size_t i=0;i<bytes.size();++i) {
        if (i==4 || i==6 || i==8 || i==10) result += '-';
        result += hex[bytes[i]>>4]; result += hex[bytes[i]&15];
    }
    return result;
}
ObservationIdentity::ObservationIdentity(std::filesystem::path path) : path_(std::move(path)) {
    if (std::filesystem::exists(path_)) {
        std::ifstream file(path_); std::string marker, number, extra;
        if (!std::getline(file,marker) || marker!="pico-observation-identity-v1" ||
            !std::getline(file,source_) || !std::getline(file,space_) || !std::getline(file,number) ||
            std::getline(file,extra) || number.empty() || number.find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("invalid observation identity file; refusing to replace identity");
        const auto revision=std::stoull(number);
        if (revision>(std::numeric_limits<uint32_t>::max)()) throw std::runtime_error("space revision overflow");
        revision_=static_cast<uint32_t>(revision);
    } else {
        source_=NewObservationUuid(); space_=source_+"/pico-map";
    }
    namespace p = monaka::protocol::v1;
    p::ObservationDeviceState probe;
    probe.version={1,0}; probe.source_id=source_; probe.device_id="identity-validation";
    probe.session_id=NewObservationUuid(); probe.clock_id=probe.session_id;
    probe.timestamp_kind="receive"; probe.presence="unknown"; probe.tracking_state="unknown";
    probe.coordinate_space={space_,"pico-output-a-v1",revision_};
    std::string encoded; p::Error error;
    if (!p::EncodeEnvelope(probe,encoded,error)) throw std::runtime_error("invalid identity: "+error.message);
    // Every PC provider lifetime invalidates the old space transform conservatively.
    AdvanceRevision();
}
uint32_t ObservationIdentity::AdvanceRevision() {
    if (revision_==(std::numeric_limits<uint32_t>::max)()) throw std::runtime_error("space revision exhausted; configure a new space id");
    const auto next=revision_+1; Save(next); revision_=next; return next;
}
void ObservationIdentity::Save(uint32_t revision) {
    if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path());
    auto temporary=path_; temporary += ".new";
    {
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        file << "pico-observation-identity-v1\n" << source_ << '\n' << space_ << '\n' << revision << '\n';
        file.flush(); if (!file) throw std::runtime_error("cannot persist observation identity");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot replace observation identity file");
#else
    std::filesystem::rename(temporary,path_);
#endif
}
}
