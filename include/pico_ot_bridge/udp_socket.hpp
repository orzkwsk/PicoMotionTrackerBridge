#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace pico_ot::bridge {

struct UdpEndpoint {
    std::string address;
    uint16_t port = 0;
};

enum class UdpReceiveStatus : uint8_t {
    Packet = 0,
    WouldBlock,
    Error,
};

class UdpSocket {
public:
    UdpSocket() = default;
    ~UdpSocket();

    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;

    UdpSocket(UdpSocket&& other) noexcept;
    UdpSocket& operator=(UdpSocket&& other) noexcept;

    // bindPort=0 asks the OS for an ephemeral port.
    bool Open(uint16_t bindPort, bool nonBlocking, std::string* error = nullptr,
              bool exclusiveBind = false, bool loopbackOnly = false);
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] uint16_t BoundPort() const noexcept { return boundPort_; }

    // v1 intentionally accepts numeric IPv4 addresses only. Discovery/name
    // resolution is a separate concern from the real-time pose path.
    bool SendTo(const UdpEndpoint& endpoint,
                const uint8_t* data,
                size_t size,
                std::string* error = nullptr);

    UdpReceiveStatus ReceiveFrom(uint8_t* buffer,
                                 size_t capacity,
                                 size_t& receivedSize,
                                 UdpEndpoint& sender,
                                 std::string* error = nullptr);

private:
    static constexpr uintptr_t kInvalidHandle = (std::numeric_limits<uintptr_t>::max)();

    uintptr_t handle_ = kInvalidHandle;
    uint16_t boundPort_ = 0;
#ifdef _WIN32
    bool winsockStarted_ = false;
#endif
};

}  // namespace pico_ot::bridge
