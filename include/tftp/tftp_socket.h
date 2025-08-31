/**
 * @file tftp_socket.h
 * @brief Cross-platform socket RAII wrapper for TFTP server
 */

#ifndef TFTP_SOCKET_H_
#define TFTP_SOCKET_H_

#include "tftp/tftp_common.h"
#include <memory>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
constexpr socket_t kInvalidSocket = INVALID_SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
using socket_t = int;
constexpr socket_t kInvalidSocket = -1;
#endif

namespace tftpserver {
namespace net {

// Forward declaration for pimpl
namespace internal {
class SocketImpl;
}

/**
 * @brief Socket address wrapper for cross-platform compatibility
 */
class TFTP_EXPORT SocketAddress {
public:
    /**
     * @brief Default constructor
     */
    SocketAddress();
    
    /**
     * @brief Constructor with IPv4 address and port
     * @param ip IPv4 address (e.g., "127.0.0.1" or INADDR_ANY)
     * @param port Port number
     */
    SocketAddress(const std::string& ip, uint16_t port);
    
    /**
     * @brief Constructor with sockaddr_in
     * @param addr Socket address structure
     */
    explicit SocketAddress(const sockaddr_in& addr);
    
    /**
     * @brief Copy constructor
     */
    SocketAddress(const SocketAddress& other);
    
    /**
     * @brief Move constructor
     */
    SocketAddress(SocketAddress&& other) noexcept;
    
    /**
     * @brief Assignment operator
     */
    SocketAddress& operator=(const SocketAddress& other);
    
    /**
     * @brief Move assignment operator
     */
    SocketAddress& operator=(SocketAddress&& other) noexcept;
    
    /**
     * @brief Destructor
     */
    ~SocketAddress();
    
    /**
     * @brief Get the underlying sockaddr_in structure
     * @return Reference to sockaddr_in
     */
    const sockaddr_in& GetSockAddr() const;
    
    /**
     * @brief Get sockaddr_in structure (mutable)
     * @return Mutable reference to sockaddr_in
     */
    sockaddr_in& GetSockAddr();
    
    /**
     * @brief Get IP address as string
     * @return IP address string
     */
    std::string GetIP() const;
    
    /**
     * @brief Get port number
     * @return Port number
     */
    uint16_t GetPort() const;
    
    /**
     * @brief Set IP address and port
     * @param ip IP address string
     * @param port Port number
     */
    void Set(const std::string& ip, uint16_t port);

private:
    std::unique_ptr<sockaddr_in> addr_;
};

/**
 * @brief Cross-platform UDP socket RAII wrapper
 * 
 * This class provides automatic resource management for UDP sockets across
 * Windows and Unix/Linux platforms. It uses the pimpl idiom to hide
 * platform-specific implementation details.
 */
class TFTP_EXPORT UdpSocket {
public:
    /**
     * @brief Default constructor - creates an invalid socket
     */
    UdpSocket();
    
    /**
     * @brief Move constructor
     */
    UdpSocket(UdpSocket&& other) noexcept;
    
    /**
     * @brief Move assignment operator
     */
    UdpSocket& operator=(UdpSocket&& other) noexcept;
    
    /**
     * @brief Destructor - automatically closes socket
     */
    ~UdpSocket();
    
    // Disable copy constructor and copy assignment
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    
    /**
     * @brief Create a UDP socket
     * @return true if successful, false on error
     */
    bool Create();
    
    /**
     * @brief Bind socket to an address
     * @param addr Address to bind to
     * @return true if successful, false on error
     */
    bool Bind(const SocketAddress& addr);
    
    /**
     * @brief Set socket option SO_REUSEADDR
     * @param reuse true to enable, false to disable
     * @return true if successful, false on error
     */
    bool SetReuseAddress(bool reuse = true);
    
    /**
     * @brief Send data to a specific address
     * @param data Data buffer to send
     * @param size Size of data to send
     * @param addr Destination address
     * @return Number of bytes sent, or -1 on error
     */
    int SendTo(const void* data, size_t size, const SocketAddress& addr);
    
    /**
     * @brief Receive data from any address
     * @param buffer Buffer to receive data
     * @param buffer_size Size of receive buffer
     * @param sender_addr Address of sender (output parameter)
     * @return Number of bytes received, or -1 on error
     */
    int ReceiveFrom(void* buffer, size_t buffer_size, SocketAddress& sender_addr);
    
    /**
     * @brief Receive data with timeout
     * @param buffer Buffer to receive data
     * @param buffer_size Size of receive buffer
     * @param sender_addr Address of sender (output parameter)
     * @param timeout_ms Timeout in milliseconds
     * @return Number of bytes received, 0 on timeout, or -1 on error
     */
    int ReceiveFromTimeout(void* buffer, size_t buffer_size, SocketAddress& sender_addr, int timeout_ms);
    
    /**
     * @brief Close the socket
     */
    void Close();
    
    /**
     * @brief Check if socket is valid
     * @return true if socket is valid and open
     */
    bool IsValid() const;
    
    /**
     * @brief Get the last error message
     * @return Error message string
     */
    std::string GetLastError() const;
    
    /**
     * @brief Get native socket handle (for advanced usage)
     * @return Native socket handle
     */
    socket_t GetNativeHandle() const;

private:
    std::unique_ptr<internal::SocketImpl> impl_;
};

/**
 * @brief Initialize socket library (Windows WSAStartup)
 * @return true if successful, false on error
 */
TFTP_EXPORT bool InitializeSocketLibrary();

/**
 * @brief Cleanup socket library (Windows WSACleanup)
 */
TFTP_EXPORT void CleanupSocketLibrary();

/**
 * @brief RAII wrapper for socket library initialization
 */
class TFTP_EXPORT SocketLibraryGuard {
public:
    /**
     * @brief Constructor - initializes socket library
     */
    SocketLibraryGuard();
    
    /**
     * @brief Destructor - cleans up socket library
     */
    ~SocketLibraryGuard();
    
    /**
     * @brief Check if initialization was successful
     * @return true if socket library is ready
     */
    bool IsInitialized() const;

private:
    bool initialized_;
};

} // namespace net
} // namespace tftpserver

#endif // TFTP_SOCKET_H_