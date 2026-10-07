#include "capture_record.hpp"
#include "pico_ot_bridge/protocol.hpp"
#include "pico_ot_bridge/udp_socket.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
using namespace pico_ot;
void Checked(bool value, int line) { if (!value) throw std::runtime_error("capture assertion failed at line " + std::to_string(line)); }
#define Check(value) Checked((value), __LINE__)
int main(int argc, char** argv) {
    try {
        bridge::PosePacket pose;
        pose.serial="synthetic-\"\\\n\xe3\x83\x86";
        pose.runtimeDeviceId=7; pose.connectedSnapshot=true; pose.positionValid=false;
        pose.orientationSamplePresent=true; pose.mappedMonotonicValid=false; pose.rawTrackingState65=2;
        pose.sourceTimestamp=UINT64_C(9007199254740993);
        pose.observedMonotonicNs=777; pose.mappedMonotonicNs=666;
        pose.positionMeters={0.1,-0.2,0.3}; pose.orientationXyzw={0.1,0.2,0.3,0.9};
        pose.referenceFrame=0; pose.angularVelocityFrame=1;
        std::vector<uint8_t> bytes;
        Check(bridge::EncodePosePacket(UINT64_MAX, 17, pose, bytes));
        const capture::HostStamp stamp{123456789,10000000};
        const auto line=capture::DatagramRecord(bytes.data(), bytes.size(), stamp, "2026-01-01T00:00:00.000Z");
        Check(line.find("\"sender_session_id\":18446744073709551615") != std::string::npos);
        Check(line.find("\"wire_sequence\":17") != std::string::npos);
        Check(line.find("\"position_valid\":false") != std::string::npos);
        Check(line.find("\"orientation_sample_present\":true") != std::string::npos);
        Check(line.find("\"pc_stopwatch_ticks\":123456789") != std::string::npos);
        Check(line.find("\"pc_stopwatch_frequency\":10000000") != std::string::npos);
        Check(line.find("\"source_timestamp_ns\":9007199254740993") != std::string::npos);
        Check(line.find("synthetic-\\\"\\\\\\u000a\xe3\x83\x86") != std::string::npos);
        Check(line.find('\n') == line.size()-1);
        if (argc == 3 && std::string(argv[1]) == "--emit") {
            std::ofstream out(argv[2]); out << line; Check(bool(out));
        }
        // Corrupt the first position double on the wire, bypassing encoder validation.
        const double nan=std::numeric_limits<double>::quiet_NaN();
        const uint64_t nanBits=UINT64_C(0x7ff8000000000000);
        for (size_t i=0;i<8;++i) bytes[92+i]=static_cast<uint8_t>(nanBits>>(8*i));
        const auto bad=capture::DatagramRecord(bytes.data(), bytes.size(), stamp, "2026-01-01T00:00:00Z");
        Check(bad.find("\"record_type\":\"decode_error\"") != std::string::npos);
        Check(bad.find("invalid_numeric_value") != std::string::npos);
        pose.positionMeters.x=nan;
        Check(!bridge::EncodePosePacket(1,1,pose,bytes));
        pose.positionMeters.x=0; pose.serial=std::string(1,static_cast<char>(0xff));
        Check(bridge::EncodePosePacket(1,1,pose,bytes));
        Check(capture::DatagramRecord(bytes.data(),bytes.size(),stamp,"").find("invalid_serial_utf8") != std::string::npos);
        bridge::DeviceStatePacket state;
        state.serial="synthetic-state"; state.charging=true; state.batteryBucketPresent=true; state.batteryBucketRaw=3;
        Check(bridge::EncodeDeviceStatePacket(42,18,state,bytes));
        const auto stateLine=capture::DatagramRecord(bytes.data(),bytes.size(),stamp,"");
        Check(stateLine.find("\"record_type\":\"device_state\"") != std::string::npos);
        Check(stateLine.find("\"charging\":true") != std::string::npos);
        Check(stateLine.find("\"battery_bucket_raw\":3") != std::string::npos);
        Check(capture::DatagramRecord(bytes.data(),1,stamp,"").find("decode_error") != std::string::npos);
        bridge::TimeSyncRequestPacket request{12,13};
        Check(bridge::EncodeTimeSyncRequestPacket(42,19,request,bytes));
        Check(capture::DatagramRecord(bytes.data(),bytes.size(),stamp,"").find("time_sync_request") != std::string::npos);
        bridge::TimeSyncResponsePacket response{12,13,14,15};
        Check(bridge::EncodeTimeSyncResponsePacket(42,20,response,bytes));
        Check(capture::DatagramRecord(bytes.data(),bytes.size(),stamp,"").find("server_send_monotonic_ns\":15") != std::string::npos);
        const auto before=capture::HostNow();
#ifdef _WIN32
        LARGE_INTEGER qpc{}, frequency{};
        Check(QueryPerformanceCounter(&qpc) && QueryPerformanceFrequency(&frequency));
        const auto after=capture::HostNow();
        // .NET Stopwatch.GetTimestamp() uses this exact unscaled QPC API.
        Check(before.ticks <= qpc.QuadPart && qpc.QuadPart <= after.ticks);
        Check(before.frequency == frequency.QuadPart);
#else
        Check(before.frequency == 1000000000);
#endif
        bridge::UdpSocket listener, other;
        std::string error;
        Check(listener.OpenExclusive(0,true,&error));
        Check(!other.OpenExclusive(listener.BoundPort(),true,&error));
        Check(!other.Open(listener.BoundPort(),true,&error));
        // Formatting benchmark only; not a real-device overhead guarantee.
        pose.serial="synthetic-benchmark";
        Check(bridge::EncodePosePacket(42,21,pose,bytes));
        const auto start=std::chrono::steady_clock::now();
        size_t total=0;
        for(int i=0;i<10000;++i) total += capture::DatagramRecord(bytes.data(),bytes.size(),stamp,"").size();
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        std::cout << "capture format benchmark: 10000 records, " << elapsed << " s, " << total << " bytes\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
