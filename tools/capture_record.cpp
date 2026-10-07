#include "capture_record.hpp"
#include "pico_ot_bridge/protocol.hpp"
#include <chrono>
#include <ctime>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

namespace pico_ot::capture {
HostStamp HostNow() {
#ifdef _WIN32
    LARGE_INTEGER ticks{}, frequency{};
    if (!QueryPerformanceCounter(&ticks) || !QueryPerformanceFrequency(&frequency))
        throw std::runtime_error("QPC unavailable");
    return {ticks.QuadPart, frequency.QuadPart};
#else
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0)
        throw std::runtime_error("CLOCK_MONOTONIC unavailable");
    return {value.tv_sec * INT64_C(1000000000) + value.tv_nsec, INT64_C(1000000000)};
#endif
}
const char* HostClockKind() {
#ifdef _WIN32
    return "windows-qpc";
#else
    return "posix-clock-monotonic";
#endif
}
std::string UtcNow() {
    const auto now = std::chrono::system_clock::now();
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now-seconds).count();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#ifdef _WIN32
    gmtime_s(&utc, &time);
#else
    gmtime_r(&time, &utc);
#endif
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << ms << 'Z';
    return out.str();
}
namespace {
// The wire accepts bytes, not necessarily UTF-8. Valid UTF-8 is preserved;
// invalid UTF-8 is diagnosed instead of emitting invalid NDJSON or replacing it.
bool Utf8(const std::string& value) {
    for (size_t i=0; i<value.size();) {
        const auto c = static_cast<unsigned char>(value[i++]);
        if (c < 0x80) continue;
        int n; uint32_t code, minimum;
        if (c >= 0xc2 && c <= 0xdf) { n=1; code=c&31; minimum=0x80; }
        else if (c >= 0xe0 && c <= 0xef) { n=2; code=c&15; minimum=0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { n=3; code=c&7; minimum=0x10000; }
        else return false;
        while (n--) {
            if (i == value.size()) return false;
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next&0xc0) != 0x80) return false;
            code=(code<<6)|(next&63);
        }
        if (code < minimum || code > 0x10ffff || (code>=0xd800 && code<=0xdfff)) return false;
    }
    return true;
}
void String(std::ostream& out, const std::string& value) {
    constexpr char hex[]="0123456789abcdef";
    out << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
        else if (c < 32) out << "\\u00" << hex[c>>4] << hex[c&15];
        else out << static_cast<char>(c);
    }
    out << '"';
}
void Vector(std::ostream& out, const bridge::Vec3d& v) { out << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
}
std::string DatagramRecord(const uint8_t* data, size_t size, HostStamp stamp, const std::string& utc) {
    if (stamp.ticks < 0 || stamp.frequency <= 0) throw std::runtime_error("invalid host stamp");
    bridge::DecodedPacket packet;
    const auto status=bridge::DecodePacket(data, size, packet);
    const auto* pose=std::get_if<bridge::PosePacket>(&packet.payload);
    const auto* state=std::get_if<bridge::DeviceStatePacket>(&packet.payload);
    const bool invalidSerial = status == bridge::DecodeStatus::Ok &&
        ((pose && !Utf8(pose->serial)) || (state && !Utf8(state->serial)));
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<double>::max_digits10) << std::boolalpha;
    out << "{\"schema_version\":1,\"pc_stopwatch_ticks\":" << stamp.ticks
        << ",\"pc_stopwatch_frequency\":" << stamp.frequency << ",\"pc_clock_kind\":";
    String(out, HostClockKind()); out << ",\"pc_utc\":"; String(out, utc);
    out << ",\"wire_size_bytes\":" << size;
    if (status != bridge::DecodeStatus::Ok || invalidSerial) {
        out << ",\"record_type\":\"decode_error\",\"decode_status\":";
        String(out, invalidSerial ? "invalid_serial_utf8" : bridge::DecodeStatusString(status));
    } else {
        out << ",\"sender_session_id\":" << packet.senderSessionId << ",\"wire_sequence\":" << packet.sequence;
        if (pose || state) {
            out << ",\"serial\":"; String(out, pose ? pose->serial : state->serial);
            out << ",\"runtime_device_id\":" << (pose ? pose->runtimeDeviceId : state->runtimeDeviceId)
                << ",\"connected_snapshot\":" << (pose ? pose->connectedSnapshot : state->connectedSnapshot)
                << ",\"hmd_observed_monotonic_ns\":" << (pose ? pose->observedMonotonicNs : state->observedMonotonicNs);
        }
        if (pose) {
            out << ",\"record_type\":\"pose\",\"position_valid\":" << pose->positionValid
                << ",\"orientation_sample_present\":" << pose->orientationSamplePresent
                << ",\"mapped_monotonic_valid\":" << pose->mappedMonotonicValid
                << ",\"raw_tracking_state65\":" << unsigned(pose->rawTrackingState65)
                << ",\"source_timestamp_ns\":" << pose->sourceTimestamp
                << ",\"hmd_mapped_monotonic_ns\":" << pose->mappedMonotonicNs << ",\"position_m\":";
            Vector(out, pose->positionMeters);
            const auto& q=pose->orientationXyzw;
            out << ",\"orientation_xyzw\":[" << q.x << ',' << q.y << ',' << q.z << ',' << q.w << ']'
                << ",\"linear_velocity_m_s\":"; Vector(out, pose->linearVelocityMetersPerSec);
            out << ",\"angular_velocity_rad_s\":"; Vector(out, pose->angularVelocityRadPerSec);
            out << ",\"reference_frame\":" << unsigned(pose->referenceFrame)
                << ",\"angular_velocity_frame\":" << unsigned(pose->angularVelocityFrame);
        } else if (state) {
            out << ",\"record_type\":\"device_state\",\"charging\":" << state->charging
                << ",\"battery_bucket_present\":" << state->batteryBucketPresent
                << ",\"battery_bucket_raw\":" << unsigned(state->batteryBucketRaw);
        } else if (const auto* request=std::get_if<bridge::TimeSyncRequestPacket>(&packet.payload)) {
            out << ",\"record_type\":\"time_sync_request\",\"nonce\":" << request->nonce
                << ",\"client_send_monotonic_ns\":" << request->clientSendMonotonicNs;
        } else if (const auto* response=std::get_if<bridge::TimeSyncResponsePacket>(&packet.payload)) {
            out << ",\"record_type\":\"time_sync_response\",\"nonce\":" << response->nonce
                << ",\"client_send_monotonic_ns\":" << response->clientSendMonotonicNs
                << ",\"server_receive_monotonic_ns\":" << response->serverReceiveMonotonicNs
                << ",\"server_send_monotonic_ns\":" << response->serverSendMonotonicNs;
        }
    }
    return out.str() + "}\n";
}
}  // namespace pico_ot::capture
