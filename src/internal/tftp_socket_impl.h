/**
 * @file tftp_socket_impl.h
 * @brief Internal implementation for cross-platform socket wrapper
 */

#ifndef TFTP_SOCKET_IMPL_H_
#define TFTP_SOCKET_IMPL_H_

#include "tftp/tftp_socket.h"
#include "tftp/tftp_logger.h"
#include <string>

// Platform-specific includes are now in separate implementation files

namespace tftpserver {
namespace net {
namespace internal {

/**
 * @brief Platform-specific socket implementation
 */
class SocketImpl {
public:
    SocketImpl();
    ~SocketImpl();
    
    // Disable copy
    SocketImpl(const SocketImpl&) = delete;
    SocketImpl& operator=(const SocketImpl&) = delete;
    
    // Enable move
    SocketImpl(SocketImpl&& other) noexcept;
    SocketImpl& operator=(SocketImpl&& other) noexcept;
    
    bool Create();
    bool Bind(const SocketAddress& addr);
    bool SetReuseAddress(bool reuse);
    int SendTo(const void* data, size_t size, const SocketAddress& addr);
    int ReceiveFrom(void* buffer, size_t buffer_size, SocketAddress& sender_addr);
    int ReceiveFromTimeout(void* buffer, size_t buffer_size, SocketAddress& sender_addr, int timeout_ms);
    void Close();
    bool IsValid() const;
    std::string GetLastError() const;
    socket_t GetNativeHandle() const;

private:
    socket_t socket_;
    mutable std::string last_error_;
    
    void SetLastError(const std::string& error);
    std::string GetSystemErrorMessage() const;
};

} // namespace internal
} // namespace net
} // namespace tftpserver

#endif // TFTP_SOCKET_IMPL_H_