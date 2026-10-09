#pragma once
#include "pico_ot_bridge/receiver.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace pico_ot {
// PC-only observer. No socket, runtime control, or wire-contract dependency.
class RuntimeDiagnostic {
public:
    using Json = nlohmann::json;
    using Writer = std::function<void(const std::string&)>;
    RuntimeDiagnostic() = default;
    ~RuntimeDiagnostic();
    bool Start(const std::string& path);
    void StartForTest(Writer writer, size_t capacity = 1024, size_t reserve = 128);
    void Stop();
    bool Enabled() const noexcept { return enabled_.load(); }
    void Poll(const bridge::Receiver& receiver, int64_t nowNs) noexcept;
    void Event(const std::string& type, Json details = Json::object(), bool critical = true) noexcept;
    uint64_t Dropped() const noexcept { return dropped_.load(); }
    bool Complete() const noexcept { return !criticalLost_.load() && !writeFailed_.load(); }
    static Json Build();
private:
    struct Tracker { uint32_t poseSequence = 0; bool poseSeen = false, firstPosition = false;
        bool registered = false; std::optional<bool> position, orientation, full;
        int32_t runtimeId = -1; };
    Json Make(const std::string& type, Json details);
    void Submit(Json value, bool critical) noexcept;
    void Worker();
    void TrackerEvent(const std::string& type, const std::string& serial, Tracker& tracker,
        Json validity = nullptr, Json details = Json::object(), bool critical = true);
    void Retire(const std::string& reason);
    std::atomic<bool> enabled_{false}, criticalLost_{false}, writeFailed_{false};
    std::atomic<uint64_t> dropped_{0}, criticalDropped_{0}, sequence_{0};
    // Single producer (service Pump thread), single consumer (file writer).
    std::mutex mutex_; std::condition_variable wake_; std::vector<Json> queue_;
    std::atomic<size_t> read_{0}, write_{0}; std::atomic<bool> stopping_{false};
    size_t capacity_ = 1024, reserve_ = 128;
    Writer writer_; std::thread worker_;
    uint64_t sender_ = 0, generation_ = 0; bool connected_ = false;
    int64_t sessionBoundaryNs_ = 0;
    std::string state_ = "connecting";
    std::unordered_map<std::string, Tracker> trackers_;
};
}
