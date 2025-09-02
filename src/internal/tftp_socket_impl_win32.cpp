/**
 * @file tftp_socket_impl_win32.cpp
 * @brief Windows-specific socket implementation
 */

#include "internal/tftp_socket_impl.h"
#include <algorithm>
#include <cctype>
#include <ws2tcpip.h>

namespace tftpserver {
namespace net {
namespace internal {

SocketImpl::SocketImpl() : socket_(kInvalidSocket) {
}

SocketImpl::~SocketImpl() {
    Close();
}

SocketImpl::SocketImpl(SocketImpl&& other) noexcept 
    : socket_(other.socket_), last_error_(std::move(other.last_error_)) {
    other.socket_ = kInvalidSocket;
}

SocketImpl& SocketImpl::operator=(SocketImpl&& other) noexcept {
    if (this != &other) {
        Close();
        socket_ = other.socket_;
        last_error_ = std::move(other.last_error_);
        other.socket_ = kInvalidSocket;
    }
    return *this;
}

bool SocketImpl::Create() {
    Close(); // Close any existing socket
    
    socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_ == kInvalidSocket) {
        SetLastError("Failed to create UDP socket: " + GetSystemErrorMessage());
        TFTP_ERROR("Failed to create UDP socket: %s", last_error_.c_str());
        return false;
    }
    
    TFTP_DEBUG("UDP socket created successfully (handle: %d)", static_cast<int>(socket_));
    return true;
}

bool SocketImpl::Bind(const SocketAddress& addr) {
    if (socket_ == kInvalidSocket) {
        SetLastError("Cannot bind invalid socket");
        return false;
    }
    
    const sockaddr_in& sock_addr = addr.GetSockAddr();
    int result = bind(socket_, reinterpret_cast<const sockaddr*>(&sock_addr), sizeof(sock_addr));
    
    if (result != 0) {
        SetLastError("Failed to bind socket: " + GetSystemErrorMessage());
        TFTP_ERROR("Failed to bind socket to %s:%d - %s", 
                   addr.GetIP().c_str(), addr.GetPort(), last_error_.c_str());
        return false;
    }
    
    TFTP_DEBUG("Socket bound to %s:%d", addr.GetIP().c_str(), addr.GetPort());
    return true;
}

bool SocketImpl::SetReuseAddress(bool reuse) {
    if (socket_ == kInvalidSocket) {
        SetLastError("Cannot set option on invalid socket");
        return false;
    }
    
    int opt = reuse ? 1 : 0;
    int result = setsockopt(socket_, SOL_SOCKET, SO_REUSEADDR, 
                           reinterpret_cast<const char*>(&opt), sizeof(opt));
    
    if (result != 0) {
        SetLastError("Failed to set SO_REUSEADDR: " + GetSystemErrorMessage());
        TFTP_WARN("Failed to set SO_REUSEADDR: %s", last_error_.c_str());
        return false;
    }
    
    TFTP_DEBUG("SO_REUSEADDR set to %s", reuse ? "true" : "false");
    return true;
}

int SocketImpl::SendTo(const void* data, size_t size, const SocketAddress& addr) {
    if (socket_ == kInvalidSocket) {
        SetLastError("Cannot send on invalid socket");
        return -1;
    }
    
    if (data == nullptr || size == 0) {
        SetLastError("Invalid send parameters");
        return -1;
    }
    
    const sockaddr_in& sock_addr = addr.GetSockAddr();
    int sent = sendto(socket_, static_cast<const char*>(data), static_cast<int>(size), 0,
                      reinterpret_cast<const sockaddr*>(&sock_addr), sizeof(sock_addr));
    
    if (sent < 0) {
        SetLastError("Failed to send data: " + GetSystemErrorMessage());
        TFTP_ERROR("Failed to send %zu bytes to %s:%d - %s", 
                   size, addr.GetIP().c_str(), addr.GetPort(), last_error_.c_str());
        return -1;
    }
    
    if (static_cast<size_t>(sent) != size) {
        TFTP_WARN("Partial send: %d bytes sent out of %zu", sent, size);
    }
    
    return sent;
}

int SocketImpl::ReceiveFrom(void* buffer, size_t buffer_size, SocketAddress& sender_addr) {
    if (socket_ == kInvalidSocket) {
        SetLastError("Cannot receive on invalid socket");
        return -1;
    }
    
    if (buffer == nullptr || buffer_size == 0) {
        SetLastError("Invalid receive parameters");
        return -1;
    }
    
    sockaddr_in& sock_addr = sender_addr.GetSockAddr();
    socklen_t addr_len = sizeof(sock_addr);
    
    int received = recvfrom(socket_, static_cast<char*>(buffer), static_cast<int>(buffer_size), 0,
                            reinterpret_cast<sockaddr*>(&sock_addr), &addr_len);
    
    if (received < 0) {
        SetLastError("Failed to receive data: " + GetSystemErrorMessage());
        TFTP_ERROR("Failed to receive data: %s", last_error_.c_str());
        return -1;
    }
    
    if (received == 0) {
        TFTP_DEBUG("Received 0 bytes (connection closed)");
    }
    
    return received;
}

int SocketImpl::ReceiveFromTimeout(void* buffer, size_t buffer_size, SocketAddress& sender_addr, int timeout_ms) {
    if (socket_ == kInvalidSocket) {
        SetLastError("Cannot receive on invalid socket");
        return -1;
    }
    
    // Use select to implement timeout
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(socket_, &readfds);
    
    struct timeval timeout;
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    // On Windows, the first parameter to select is ignored
    int result = select(0, &readfds, nullptr, nullptr, &timeout);
    
    if (result < 0) {
        SetLastError("Select failed: " + GetSystemErrorMessage());
        TFTP_ERROR("Select failed: %s", last_error_.c_str());
        return -1;
    }
    
    if (result == 0) {
        // Timeout occurred
        SetLastError("Receive timeout");
        TFTP_DEBUG("Receive timeout after %d ms", timeout_ms);
        return 0;
    }
    
    // Data is available, receive it
    return ReceiveFrom(buffer, buffer_size, sender_addr);
}

void SocketImpl::Close() {
    if (socket_ != kInvalidSocket) {
        TFTP_DEBUG("Closing Windows socket (handle: %d)", static_cast<int>(socket_));
        closesocket(socket_);
        socket_ = kInvalidSocket;
    }
}

bool SocketImpl::IsValid() const {
    return socket_ != kInvalidSocket;
}

std::string SocketImpl::GetLastError() const {
    return last_error_;
}

socket_t SocketImpl::GetNativeHandle() const {
    return socket_;
}

void SocketImpl::SetLastError(const std::string& error) {
    last_error_ = error;
}

std::string SocketImpl::GetSystemErrorMessage() const {
    int error_code = WSAGetLastError();
    LPSTR message_buffer = nullptr;
    size_t size = FormatMessageA(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error_code, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPSTR>(&message_buffer), 0, nullptr);
    
    std::string message;
    if (size > 0) {
        message.assign(message_buffer, size);
        // Remove trailing newlines
        message.erase(std::find_if(message.rbegin(), message.rend(),
                                   [](unsigned char ch) { return !std::isspace(ch); }).base(),
                      message.end());
    } else {
        message = "WSA Error code " + std::to_string(error_code);
    }
    
    LocalFree(message_buffer);
    return message;
}

} // namespace internal
} // namespace net
} // namespace tftpserver