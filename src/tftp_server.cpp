#include "tftp/tftp_server.h"
#include "internal/tftp_server_impl.h"

namespace tftpserver {

TftpServer::TftpServer(const std::string& root_dir, uint16_t port)
    : impl_(std::make_unique<internal::TftpServerImpl>(root_dir, port)) {}

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
        return;
    }
    impl_->SetReadCallback(std::move(callback));
}

void TftpServer::SetWriteCallback(std::function<bool(const std::string&, const std::vector<uint8_t>&)> callback) {
    if (!impl_) {
        return;
    }
    impl_->SetWriteCallback(std::move(callback));
}

void TftpServer::SetSecureMode(bool secure) {
    if (!impl_) {
        return;
    }
    impl_->SetSecureMode(secure);
}

void TftpServer::SetMaxTransferSize(size_t size) {
    if (!impl_) {
        return;
    }
    impl_->SetMaxTransferSize(size);
}

void TftpServer::SetTimeout(int seconds) {
    if (!impl_) {
        return;
    }
    impl_->SetTimeout(seconds);
}

} // namespace tftpserver
