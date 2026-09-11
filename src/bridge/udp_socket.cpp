#include "pico_ot_bridge/udp_socket.hpp"

#include <cstring>
#include <utility>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace pico_ot::bridge {
namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kNativeInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kNativeInvalidSocket = -1;
#endif

NativeSocket ToNative(uintptr_t handle) {
    if (handle == std::numeric_limits<uintptr_t>::max()) {
        return kNativeInvalidSocket;
    }
    return static_cast<NativeSocket>(handle);
}

uintptr_t FromNative(NativeSocket handle) {
    if (handle == kNativeInvalidSocket) {
        return std::numeric_limits<uintptr_t>::max();
    }
    return static_cast<uintptr_t>(handle);
}

void SetError(std::string* error, const char* prefix) {
    if (!error) {
        return;
    }
#ifdef _WIN32
    *error = std::string(prefix) + " (WSA=" + std::to_string(WSAGetLastError()) + ")";
#else
    *error = std::string(prefix) + ": " + std::strerror(errno);
#endif
}

}  // namespace

UdpSocket::~UdpSocket() {
    Close();
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept
    : handle_(other.handle_), boundPort_(other.boundPort_)
#ifdef _WIN32
    , winsockStarted_(other.winsockStarted_)
#endif
{
    other.handle_ = kInvalidHandle;
    other.boundPort_ = 0;
#ifdef _WIN32
    other.winsockStarted_ = false;
#endif
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    Close();
    handle_ = other.handle_;
    boundPort_ = other.boundPort_;
#ifdef _WIN32
    winsockStarted_ = other.winsockStarted_;
    other.winsockStarted_ = false;
#endif
    other.handle_ = kInvalidHandle;
    other.boundPort_ = 0;
    return *this;
}

bool UdpSocket::Open(uint16_t bindPort, bool nonBlocking, std::string* error) {
    Close();

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        SetError(error, "WSAStartup failed");
        return false;
    }
    winsockStarted_ = true;
#endif

    const NativeSocket socketHandle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socketHandle == kNativeInvalidSocket) {
        SetError(error, "socket failed");
        Close();
        return false;
    }
    handle_ = FromNative(socketHandle);

    int reuse = 1;
#ifdef _WIN32
    ::setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));
#else
    ::setsockopt(socketHandle, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(bindPort);

    if (::bind(socketHandle, reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        SetError(error, "bind failed");
        Close();
        return false;
    }

    sockaddr_in actual{};
#ifdef _WIN32
    int actualSize = sizeof(actual);
#else
    socklen_t actualSize = sizeof(actual);
#endif
    if (::getsockname(socketHandle, reinterpret_cast<sockaddr*>(&actual), &actualSize) != 0) {
        SetError(error, "getsockname failed");
        Close();
        return false;
    }
    boundPort_ = ntohs(actual.sin_port);

    if (nonBlocking) {
#ifdef _WIN32
        u_long enabled = 1;
        if (::ioctlsocket(socketHandle, FIONBIO, &enabled) != 0) {
            SetError(error, "ioctlsocket(FIONBIO) failed");
            Close();
            return false;
        }
#else
        const int flags = ::fcntl(socketHandle, F_GETFL, 0);
        if (flags < 0 || ::fcntl(socketHandle, F_SETFL, flags | O_NONBLOCK) != 0) {
            SetError(error, "fcntl(O_NONBLOCK) failed");
            Close();
            return false;
        }
#endif
    }

    return true;
}

void UdpSocket::Close() noexcept {
    const NativeSocket socketHandle = ToNative(handle_);
    if (socketHandle != kNativeInvalidSocket) {
#ifdef _WIN32
        ::closesocket(socketHandle);
#else
        ::close(socketHandle);
#endif
    }
    handle_ = kInvalidHandle;
    boundPort_ = 0;

#ifdef _WIN32
    if (winsockStarted_) {
        ::WSACleanup();
        winsockStarted_ = false;
    }
#endif
}

bool UdpSocket::IsOpen() const noexcept {
    return handle_ != kInvalidHandle;
}

bool UdpSocket::SendTo(const UdpEndpoint& endpoint,
                       const uint8_t* data,
                       size_t size,
                       std::string* error) {
    if (!IsOpen() || data == nullptr || size == 0 || endpoint.port == 0) {
        if (error) {
            *error = "invalid UDP send arguments";
        }
        return false;
    }

    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(endpoint.port);
    if (::inet_pton(AF_INET, endpoint.address.c_str(), &remote.sin_addr) != 1) {
        if (error) {
            *error = "UDP endpoint must be a numeric IPv4 address";
        }
        return false;
    }

    const NativeSocket socketHandle = ToNative(handle_);
#ifdef _WIN32
    if (size > static_cast<size_t>(std::numeric_limits<int>::max())) {
        if (error) {
            *error = "UDP datagram too large";
        }
        return false;
    }
    const int sent = ::sendto(socketHandle,
                              reinterpret_cast<const char*>(data),
                              static_cast<int>(size),
                              0,
                              reinterpret_cast<const sockaddr*>(&remote),
                              sizeof(remote));
    if (sent == SOCKET_ERROR || static_cast<size_t>(sent) != size) {
#else
    const ssize_t sent = ::sendto(socketHandle,
                                  data,
                                  size,
                                  0,
                                  reinterpret_cast<const sockaddr*>(&remote),
                                  sizeof(remote));
    if (sent < 0 || static_cast<size_t>(sent) != size) {
#endif
        SetError(error, "sendto failed");
        return false;
    }
    return true;
}

UdpReceiveStatus UdpSocket::ReceiveFrom(uint8_t* buffer,
                                        size_t capacity,
                                        size_t& receivedSize,
                                        UdpEndpoint& sender,
                                        std::string* error) {
    receivedSize = 0;
    sender = {};

    if (!IsOpen() || buffer == nullptr || capacity == 0) {
        if (error) {
            *error = "invalid UDP receive arguments";
        }
        return UdpReceiveStatus::Error;
    }

    sockaddr_in remote{};
#ifdef _WIN32
    int remoteSize = sizeof(remote);
    if (capacity > static_cast<size_t>(std::numeric_limits<int>::max())) {
        if (error) {
            *error = "UDP receive buffer too large";
        }
        return UdpReceiveStatus::Error;
    }
    const int received = ::recvfrom(ToNative(handle_),
                                    reinterpret_cast<char*>(buffer),
                                    static_cast<int>(capacity),
                                    0,
                                    reinterpret_cast<sockaddr*>(&remote),
                                    &remoteSize);
    if (received == SOCKET_ERROR) {
        const int code = WSAGetLastError();
        if (code == WSAEWOULDBLOCK) {
            return UdpReceiveStatus::WouldBlock;
        }
#else
    socklen_t remoteSize = sizeof(remote);
    const ssize_t received = ::recvfrom(ToNative(handle_),
                                        buffer,
                                        capacity,
                                        0,
                                        reinterpret_cast<sockaddr*>(&remote),
                                        &remoteSize);
    if (received < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return UdpReceiveStatus::WouldBlock;
        }
#endif
        SetError(error, "recvfrom failed");
        return UdpReceiveStatus::Error;
    }

    char address[INET_ADDRSTRLEN]{};
    if (::inet_ntop(AF_INET, &remote.sin_addr, address, sizeof(address)) == nullptr) {
        SetError(error, "inet_ntop failed");
        return UdpReceiveStatus::Error;
    }

    receivedSize = static_cast<size_t>(received);
    sender.address = address;
    sender.port = ntohs(remote.sin_port);
    return UdpReceiveStatus::Packet;
}

}  // namespace pico_ot::bridge
