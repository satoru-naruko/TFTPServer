/**
 * @file tftp_server_impl.h
 * @brief TFTP server implementation class
 */

#ifndef TFTP_SERVER_IMPL_H_
#define TFTP_SERVER_IMPL_H_

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

#include "tftp/tftp_common.h"
#include "tftp/tftp_packet.h"
#include "tftp/tftp_logger.h"
#include "internal/tftp_thread_pool.h"
#include <string>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include <filesystem>

namespace tftpserver {
namespace internal {

class TftpServerImpl {
public:
    TftpServerImpl(const std::string& root_dir, uint16_t port);
    ~TftpServerImpl();

    bool Start();
    void Stop();
    bool IsRunning() const;

    void SetReadCallback(std::function<bool(const std::string&, std::vector<uint8_t>&)> callback) {
        read_callback_ = std::move(callback);
    }

    void SetWriteCallback(std::function<bool(const std::string&, const std::vector<uint8_t>&)> callback) {
        write_callback_ = std::move(callback);
    }

    void SetSecureMode(bool secure) { secure_mode_ = secure; }
    void SetMaxTransferSize(size_t size) { max_transfer_size_ = size; }
    void SetTimeout(int seconds) { timeout_seconds_ = seconds; }
    void SetThreadPoolSize(size_t size) { thread_pool_size_ = size; }

private:
    void ServerLoop();
    void HandleClient(const std::vector<uint8_t>& initial_packet, const sockaddr_in& client_addr);
    
    void HandleReadRequest(
#ifdef _WIN32
        SOCKET sock,
#else
        int sock,
#endif
        const sockaddr_in& client_addr,
        const std::string& filepath, TransferMode mode);
        
    void HandleWriteRequest(
#ifdef _WIN32
        SOCKET sock,
#else
        int sock,
#endif
        const sockaddr_in& client_addr,
        const std::string& filepath, TransferMode mode, const TftpPacket& packet);
        
    bool SendPacket(
#ifdef _WIN32
        SOCKET sock,
#else
        int sock,
#endif
        const sockaddr_in& addr, const TftpPacket& packet);
        
    bool ReceivePacket(
#ifdef _WIN32
        SOCKET sock,
#else
        int sock,
#endif
        sockaddr_in& addr, TftpPacket& packet, int timeout_ms);
        
    void SendError(
#ifdef _WIN32
        SOCKET sock,
#else
        int sock,
#endif
        const sockaddr_in& addr, ErrorCode code, const std::string& message);
    
    // File I/O processing callback handlers
    static bool DefaultReadHandler(const std::string& path, std::vector<uint8_t>& data);
    static bool DefaultWriteHandler(const std::string& path, const std::vector<uint8_t>& data);

    std::string root_dir_;
    uint16_t port_;
#ifdef _WIN32
    SOCKET sockfd_;
#else
    int sockfd_;
#endif
    std::atomic<bool> running_;
    std::thread server_thread_;
    std::unique_ptr<TftpThreadPool> thread_pool_;
    bool secure_mode_;
    size_t max_transfer_size_;
    int timeout_seconds_;
    size_t thread_pool_size_;

    std::function<bool(const std::string&, std::vector<uint8_t>&)> read_callback_;
    std::function<bool(const std::string&, const std::vector<uint8_t>&)> write_callback_;
};

} // namespace internal
} // namespace tftpserver

#endif // TFTP_SERVER_IMPL_H_ 