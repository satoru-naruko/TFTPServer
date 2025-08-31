#include "tftp/tftp_socket.h"
#include "internal/tftp_socket_impl.h"
#include <cstring>
#include <sstream>

namespace tftpserver {
namespace net {

// SocketAddress implementation
SocketAddress::SocketAddress() : addr_(std::make_unique<sockaddr_in>()) {
    std::memset(addr_.get(), 0, sizeof(sockaddr_in));
    addr_->sin_family = AF_INET;
    addr_->sin_addr.s_addr = htonl(INADDR_ANY);
    addr_->sin_port = htons(0);
}

SocketAddress::SocketAddress(const std::string& ip, uint16_t port) : SocketAddress() {
    Set(ip, port);
}

SocketAddress::SocketAddress(const sockaddr_in& addr) : addr_(std::make_unique<sockaddr_in>(addr)) {
}

SocketAddress::SocketAddress(const SocketAddress& other) 
    : addr_(std::make_unique<sockaddr_in>(*other.addr_)) {
}

SocketAddress::SocketAddress(SocketAddress&& other) noexcept 
    : addr_(std::move(other.addr_)) {
}

SocketAddress& SocketAddress::operator=(const SocketAddress& other) {
    if (this != &other) {
        *addr_ = *other.addr_;
    }
    return *this;
}

SocketAddress& SocketAddress::operator=(SocketAddress&& other) noexcept {
    if (this != &other) {
        addr_ = std::move(other.addr_);
    }
    return *this;
}

SocketAddress::~SocketAddress() = default;

const sockaddr_in& SocketAddress::GetSockAddr() const {
    return *addr_;
}

sockaddr_in& SocketAddress::GetSockAddr() {
    return *addr_;
}

std::string SocketAddress::GetIP() const {
    char ip_str[INET_ADDRSTRLEN];
#ifdef _WIN32
    if (inet_ntop(AF_INET, &addr_->sin_addr, ip_str, INET_ADDRSTRLEN) != nullptr) {
        return std::string(ip_str);
    }
#else
    if (inet_ntop(AF_INET, &addr_->sin_addr, ip_str, INET_ADDRSTRLEN) != nullptr) {
        return std::string(ip_str);
    }
#endif
    return "0.0.0.0";
}

uint16_t SocketAddress::GetPort() const {
    return ntohs(addr_->sin_port);
}

void SocketAddress::Set(const std::string& ip, uint16_t port) {
    addr_->sin_family = AF_INET;
    addr_->sin_port = htons(port);
    
    if (ip.empty() || ip == "0.0.0.0") {
        addr_->sin_addr.s_addr = htonl(INADDR_ANY);
    } else {
#ifdef _WIN32
        if (inet_pton(AF_INET, ip.c_str(), &addr_->sin_addr) != 1) {
            // Failed to parse IP, use INADDR_ANY
            addr_->sin_addr.s_addr = htonl(INADDR_ANY);
        }
#else
        if (inet_pton(AF_INET, ip.c_str(), &addr_->sin_addr) != 1) {
            // Failed to parse IP, use INADDR_ANY
            addr_->sin_addr.s_addr = htonl(INADDR_ANY);
        }
#endif
    }
}

// UdpSocket implementation
UdpSocket::UdpSocket() : impl_(std::make_unique<internal::SocketImpl>()) {
}

UdpSocket::UdpSocket(UdpSocket&& other) noexcept : impl_(std::move(other.impl_)) {
}

UdpSocket& UdpSocket::operator=(UdpSocket&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
    }
    return *this;
}

UdpSocket::~UdpSocket() = default;

bool UdpSocket::Create() {
    return impl_->Create();
}

bool UdpSocket::Bind(const SocketAddress& addr) {
    return impl_->Bind(addr);
}

bool UdpSocket::SetReuseAddress(bool reuse) {
    return impl_->SetReuseAddress(reuse);
}

int UdpSocket::SendTo(const void* data, size_t size, const SocketAddress& addr) {
    return impl_->SendTo(data, size, addr);
}

int UdpSocket::ReceiveFrom(void* buffer, size_t buffer_size, SocketAddress& sender_addr) {
    return impl_->ReceiveFrom(buffer, buffer_size, sender_addr);
}

int UdpSocket::ReceiveFromTimeout(void* buffer, size_t buffer_size, SocketAddress& sender_addr, int timeout_ms) {
    return impl_->ReceiveFromTimeout(buffer, buffer_size, sender_addr, timeout_ms);
}

void UdpSocket::Close() {
    impl_->Close();
}

bool UdpSocket::IsValid() const {
    return impl_->IsValid();
}

std::string UdpSocket::GetLastError() const {
    return impl_->GetLastError();
}

socket_t UdpSocket::GetNativeHandle() const {
    return impl_->GetNativeHandle();
}

// Socket library management
bool InitializeSocketLibrary() {
#ifdef _WIN32
    WSADATA wsaData;
    int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (result != 0) {
        TFTP_ERROR("WSAStartup failed with error: %d", result);
        return false;
    }
    TFTP_DEBUG("WSAStartup successful");
    return true;
#else
    // On Unix/Linux, no initialization needed
    return true;
#endif
}

void CleanupSocketLibrary() {
#ifdef _WIN32
    WSACleanup();
    TFTP_DEBUG("WSACleanup called");
#else
    // On Unix/Linux, no cleanup needed
#endif
}

// SocketLibraryGuard implementation
SocketLibraryGuard::SocketLibraryGuard() : initialized_(false) {
    initialized_ = InitializeSocketLibrary();
}

SocketLibraryGuard::~SocketLibraryGuard() {
    if (initialized_) {
        CleanupSocketLibrary();
    }
}

bool SocketLibraryGuard::IsInitialized() const {
    return initialized_;
}

} // namespace net
} // namespace tftpserver