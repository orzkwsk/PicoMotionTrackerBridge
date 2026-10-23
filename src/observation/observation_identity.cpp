#include "pico_ot_backend/observation_identity.hpp"
#include "monaka/protocol/v2/codec.hpp"
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
        if (!std::getline(file,marker) || (marker!="pico-observation-identity-v1"&&marker!="pico-observation-identity-v2") ||
            !std::getline(file,source_) || !std::getline(file,space_) || !std::getline(file,number) ||
            number.empty() || number.find_first_not_of("0123456789")!=std::string::npos)
            throw std::runtime_error("invalid observation identity file; refusing to replace identity");
        const auto revision=std::stoull(number);
        if (revision>(std::numeric_limits<uint32_t>::max)()) throw std::runtime_error("space revision overflow");
        revision_=static_cast<uint32_t>(revision);
        if(marker=="pico-observation-identity-v2"){
            std::string status;
            if(!std::getline(file,generation_)||!std::getline(file,status)||(status!="known"&&status!="unknown")||generation_.size()>128)
                throw std::runtime_error("invalid source-space state");
            known_=status=="known";
            if(known_&&generation_.empty())throw std::runtime_error("known space requires generation");
        }
        if(std::getline(file,extra))throw std::runtime_error("extra observation identity state");
    } else {
        source_=NewObservationUuid(); space_=source_+"/pico-map";
    }
    namespace p = monaka::protocol::v2;
    p::ObservationDeviceState probe;
    probe.version={2,0}; probe.modality="none"; probe.source_id=source_; probe.device_id="identity-validation";
    probe.session_id=NewObservationUuid(); probe.clock_id=probe.session_id;
    probe.timestamp_kind="receive"; probe.presence="unknown"; probe.tracking_state="unknown";
    probe.coordinate_space={space_,"pico-output-a-v1",revision_};
    std::string encoded; p::Error error;
    if (!p::EncodeEnvelope(probe,encoded,error)) throw std::runtime_error("invalid identity: "+error.message);
    // PC lifetime is not a coordinate-space generation. A new identity starts
    // unknown; a legacy identity keeps its revision until explicit adoption.
    if(!std::filesystem::exists(path_))Save(revision_);
}
uint32_t ObservationIdentity::AdvanceRevision() {
    if (revision_==(std::numeric_limits<uint32_t>::max)()) throw std::runtime_error("space revision exhausted; configure a new space id");
    const auto next=revision_+1; Save(next); revision_=next; return next;
}
SourceSpaceState ObservationIdentity::RefreshSpace(uint64_t senderSession) {
    // The PICO transport currently has no runtime space-generation indication.
    // An explicit declaration names a space generation independently of sender
    // sessions; an unmatched session is UNKNOWN, never silently continuous.
    auto declaration=path_;declaration+=".space";
    bool declared=false;std::string nextGeneration;
    if(std::filesystem::exists(declaration)){
        if(std::filesystem::file_size(declaration)>2048)throw std::runtime_error("space declaration too large");
        std::ifstream f(declaration);std::string marker,source,space,sender,status,extra;
        if(!std::getline(f,marker)||!std::getline(f,source)||!std::getline(f,space)||!std::getline(f,nextGeneration)||!std::getline(f,sender)||!std::getline(f,status)||std::getline(f,extra)||marker!="pico-source-space-declaration-v1"||source!=source_||space!=space_||nextGeneration.empty()||nextGeneration.size()>128||sender.empty()||sender.find_first_not_of("0123456789")!=std::string::npos||(status!="known"&&status!="unknown"))
            throw std::runtime_error("invalid source-space declaration; output disabled");
        declared=status=="known"&&std::stoull(sender)==senderSession;
    }
    const bool changed=declared&&nextGeneration!=generation_;
    const bool becameUnknown=!declared&&known_;
    if(changed||becameUnknown||declared!=known_){
        const auto previousGeneration=generation_;const bool previousKnown=known_;
        generation_=declared?nextGeneration:generation_;known_=declared;
        try { if(changed||becameUnknown)AdvanceRevision();else Save(revision_); }
        catch(...){generation_=previousGeneration;known_=previousKnown;throw;}
    }
    return {revision_,known_};
}
void ObservationIdentity::Save(uint32_t revision) {
    if (!path_.parent_path().empty()) std::filesystem::create_directories(path_.parent_path());
    auto temporary=path_; temporary += ".new-"+NewObservationUuid();
    struct Cleanup { std::filesystem::path p; ~Cleanup(){std::error_code e;std::filesystem::remove(p,e);} } cleanup{temporary};
    {
        std::ofstream file(temporary,std::ios::binary|std::ios::trunc);
        file << "pico-observation-identity-v2\n" << source_ << '\n' << space_ << '\n' << revision << '\n' << generation_ << '\n' << (known_?"known":"unknown") << '\n';
        file.flush(); if (!file) throw std::runtime_error("cannot persist observation identity");
    }
#ifdef _WIN32
    HANDLE pending=CreateFileW(temporary.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(pending==INVALID_HANDLE_VALUE)throw std::runtime_error("cannot flush observation identity");
    const bool flushed=FlushFileBuffers(pending)!=0;CloseHandle(pending);
    if(!flushed)throw std::runtime_error("cannot flush observation identity");
    if (!MoveFileExW(temporary.c_str(),path_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("cannot replace observation identity file");
#else
    std::filesystem::rename(temporary,path_);
#endif
}
}
