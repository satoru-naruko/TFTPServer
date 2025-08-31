#include "tftp/tftp_server.h"
#include "tftp/tftp_validation.h"
#include "tftp/tftp_logger.h"
#include "internal/tftp_server_impl.h"

namespace tftpserver {

TftpServer::TftpServer(const std::string& root_dir, uint16_t port)
    : impl_(nullptr) {
    // Validate constructor parameters
    if (!validation::ValidateRootDirectory(root_dir)) {
        TFTP_ERROR("TftpServer constructor: invalid root directory");
        throw TftpException("Invalid root directory: " + root_dir);
    }
    
    if (!validation::ValidatePort(port)) {
        TFTP_ERROR("TftpServer constructor: invalid port number");
        throw TftpException("Invalid port number: " + std::to_string(port));
    }
    
    impl_ = std::make_unique<internal::TftpServerImpl>(root_dir, port);
}

TftpServer::~TftpServer() = default;

bool TftpServer::Start() {
    if (!impl_) {
        return false;
    }
    return impl_->Start();
}

void TftpServer::Stop() {
    if (!impl_) {
        return;
    }
    impl_->Stop();
}

bool TftpServer::IsRunning() const {
    if (!impl_) {
        return false;
    }
    return impl_->IsRunning();
}

void TftpServer::SetReadCallback(std::function<bool(const std::string&, std::vector<uint8_t>&)> callback) {
    if (!impl_) {
        TFTP_ERROR("SetReadCallback: server not initialized");
        return;
    }
    
    if (!validation::ValidateCallback(callback)) {
        TFTP_ERROR("SetReadCallback: callback is null");
        throw TftpException("Read callback cannot be null");
    }
    
    impl_->SetReadCallback(std::move(callback));
}

void TftpServer::SetWriteCallback(std::function<bool(const std::string&, const std::vector<uint8_t>&)> callback) {
    if (!impl_) {
        TFTP_ERROR("SetWriteCallback: server not initialized");
        return;
    }
    
    if (!validation::ValidateCallback(callback)) {
        TFTP_ERROR("SetWriteCallback: callback is null");
        throw TftpException("Write callback cannot be null");
    }
    
    impl_->SetWriteCallback(std::move(callback));
}

void TftpServer::SetSecureMode(bool secure) {
    if (!impl_) {
        TFTP_ERROR("SetSecureMode: server not initialized");
        return;
    }
    
    // Note: bool values are always valid, no validation needed
    impl_->SetSecureMode(secure);
}

void TftpServer::SetMaxTransferSize(size_t size) {
    if (!impl_) {
        TFTP_ERROR("SetMaxTransferSize: server not initialized");
        return;
    }
    
    if (!validation::ValidateTransferSize(size)) {
        TFTP_ERROR("SetMaxTransferSize: invalid transfer size");
        throw TftpException("Invalid transfer size: " + std::to_string(size));
    }
    
    impl_->SetMaxTransferSize(size);
}

void TftpServer::SetTimeout(int seconds) {
    if (!impl_) {
        TFTP_ERROR("SetTimeout: server not initialized");
        return;
    }
    
    if (!validation::ValidateTimeout(seconds)) {
        TFTP_ERROR("SetTimeout: invalid timeout value");
        throw TftpException("Invalid timeout: " + std::to_string(seconds));
    }
    
    impl_->SetTimeout(seconds);
}

} // namespace tftpserver
