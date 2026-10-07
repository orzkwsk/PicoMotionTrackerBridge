#include "capture_record.hpp"
#include "pico_ot_bridge/udp_socket.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
volatile std::sig_atomic_t stopping=0;
void Stop(int) { stopping=1; }
uint64_t Number(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("expected unsigned decimal argument");
    return std::stoull(text);
}
int Run(int argc, char** argv) {
    uint64_t port=0, duration=0, flushMs=1000, expectedFrequency=0;
    std::filesystem::path output, stopFile, readyFile, completionFile;
    for (int i=1; i<argc; ++i) {
        const std::string arg=argv[i];
        if (arg == "--help") {
            std::cerr << "pico_bridge_capture_cli --port PORT --output PATH [--duration-sec N]"
                         " [--flush-interval MS] [--stop-file PATH] [--ready-file PATH]"
                         " [--expected-frequency N] [--completion-file PATH]\n";
            return 0;
        }
        if (++i == argc) throw std::runtime_error("missing option value");
        const std::string value=argv[i];
        if (arg == "--port") port=Number(value);
        else if (arg == "--output") output=std::filesystem::u8path(value);
        else if (arg == "--duration-sec") duration=Number(value);
        else if (arg == "--flush-interval") flushMs=Number(value);
        else if (arg == "--expected-frequency") expectedFrequency=Number(value);
        else if (arg == "--stop-file") stopFile=std::filesystem::u8path(value);
        else if (arg == "--ready-file") readyFile=std::filesystem::u8path(value);
        else if (arg == "--completion-file") completionFile=std::filesystem::u8path(value);
        else throw std::runtime_error("unknown option");
    }
    if (port == 0 || port > 65535 || output.empty() || flushMs == 0 || flushMs > 60000 || duration > 86400)
        throw std::runtime_error("invalid port/output/duration/flush interval");
    const auto initial=pico_ot::capture::HostNow();
    if (expectedFrequency && expectedFrequency != static_cast<uint64_t>(initial.frequency))
        throw std::runtime_error("QPC frequency does not match run manifest");
    for (const auto& path : {stopFile, readyFile, completionFile}) {
        if (!path.empty() && (path == output || std::filesystem::exists(path)))
            throw std::runtime_error("lifecycle path exists or conflicts with output");
    }
    if (!stopFile.empty() && stopFile == readyFile) throw std::runtime_error("lifecycle paths conflict");
    if (!completionFile.empty() && (completionFile == stopFile || completionFile == readyFile))
        throw std::runtime_error("lifecycle paths conflict");
    pico_ot::bridge::UdpSocket socket;
    std::string error;
    if (!socket.OpenExclusive(static_cast<uint16_t>(port), true, &error)) throw std::runtime_error(error);
    // Exclusive file creation prevents truncating a prior capture. One buffered
    // sequential write per datagram; no pose stdout or per-packet flush/stat.
    std::vector<char> fileBuffer(1024*1024);
#ifdef _WIN32
    FILE* file=nullptr;
    _wfopen_s(&file, output.c_str(), L"wbx");
#else
    FILE* file=std::fopen(output.c_str(), "wbx");
#endif
    if (!file) throw std::runtime_error("cannot create new capture file");
    std::unique_ptr<FILE, decltype(&std::fclose)> handle(file, &std::fclose);
    if (std::setvbuf(file, fileBuffer.data(), _IOFBF, fileBuffer.size()) != 0)
        throw std::runtime_error("cannot buffer capture file");
    std::signal(SIGINT, Stop);
    std::signal(SIGTERM, Stop);
    if (!readyFile.empty()) {
        std::ofstream ready(readyFile);
        ready << "{\"pc_stopwatch_frequency\":" << initial.frequency << ",\"pc_clock_kind\":\""
              << pico_ot::capture::HostClockKind() << "\"}\n";
        ready.close();
        if (!ready) throw std::runtime_error("cannot publish readiness");
    }
    using Clock=std::chrono::steady_clock;
    const auto started=Clock::now();
    auto lastFlush=started, lastControl=started;
    std::array<uint8_t, 65536> buffer{}; // Entire legal IPv4 UDP datagram, including malformed packets.
    uint64_t count=0;
    while (!stopping) {
        size_t size=0;
        pico_ot::bridge::UdpEndpoint sender;
        const auto status=socket.ReceiveFrom(buffer.data(), buffer.size(), size, sender, &error);
        if (status == pico_ot::bridge::UdpReceiveStatus::Packet) {
            // First operation after ReceiveFrom, before DecodePacket. Windows raw
            // QPC ticks agree with .NET Stopwatch.GetTimestamp() on this PC.
            const auto stamp=pico_ot::capture::HostNow();
            const auto record=pico_ot::capture::DatagramRecord(buffer.data(), size, stamp, pico_ot::capture::UtcNow());
            if (std::fwrite(record.data(), 1, record.size(), file) != record.size())
                throw std::runtime_error("capture write failed");
            ++count;
        } else if (status == pico_ot::bridge::UdpReceiveStatus::Error) throw std::runtime_error(error);
        else std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const auto now=Clock::now();
        if (now-lastFlush >= std::chrono::milliseconds(flushMs)) {
            if (std::fflush(file) != 0) throw std::runtime_error("capture flush failed");
            lastFlush=now;
        }
        if (now-lastControl >= std::chrono::milliseconds(100)) {
            if (!stopFile.empty() && std::filesystem::exists(stopFile)) break;
            if (duration && now-started >= std::chrono::seconds(duration)) break;
            lastControl=now;
        }
    }
    if (std::fflush(file) != 0) throw std::runtime_error("capture final flush failed");
    // Close while the supplied stdio buffer still exists. fclose errors are fatal.
    FILE* closing=handle.release();
    if (std::fclose(closing) != 0) throw std::runtime_error("capture close failed");
    if (!completionFile.empty()) {
        std::ofstream completed(completionFile);
        completed << "{\"exit_code\":0,\"datagrams\":" << count << "}\n";
        completed.close();
        if (!completed) throw std::runtime_error("cannot publish completion");
    }
    std::cerr << "capture completed: datagrams=" << count << " frequency=" << initial.frequency << '\n';
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** wideArgv) {
    std::vector<std::string> args;
    std::vector<char*> argv;
    for (int i=0;i<argc;++i) {
        const int size=WideCharToMultiByte(CP_UTF8, 0, wideArgv[i], -1, nullptr, 0, nullptr, nullptr);
        if (size == 0) return 2;
        std::string text(static_cast<size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, 0, wideArgv[i], -1, text.data(), size, nullptr, nullptr);
        text.pop_back(); args.push_back(std::move(text));
    }
    for(auto& arg : args) argv.push_back(arg.data());
    try { return Run(argc, argv.data()); }
#else
int main(int argc, char** argv) {
    try { return Run(argc, argv); }
#endif
    catch (const std::exception& e) { std::cerr << "capture error: " << e.what() << '\n'; return 2; }
}
