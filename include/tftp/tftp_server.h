/**
 * @file tftp_server.h
 * @brief TFTP server functionality interface
 */

#ifndef TFTP_SERVER_H_
#define TFTP_SERVER_H_

#include "tftp/tftp_common.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace tftpserver {

// Forward declaration
namespace internal {
class TftpServerImpl;
}

/**
 * @class TftpServer
 * @brief Class that provides TFTP server functionality
 */
class TFTP_EXPORT TftpServer {
 public:
  /**
   * @brief Constructor
   * @param root_dir Root directory
   * @param port Port number (default is 69)
   */
  TftpServer(const std::string& root_dir, uint16_t port = kDefaultTftpPort);

  /**
   * @brief Destructor
   */
  ~TftpServer();

  /**
   * @brief Start the server
   * @return true if successful, false if failed
   */
  bool Start();

  /**
   * @brief Stop the server
   */
  void Stop();

  /**
   * @brief Check if server is running
   * @return true if running
   */
  bool IsRunning() const;

  /**
   * @brief Set file read callback
   * @param callback File read callback function
   */
  void SetReadCallback(std::function<bool(const std::string&, std::vector<uint8_t>&)> callback);

  /**
   * @brief Set file write callback
   * @param callback File write callback function
   */
  void SetWriteCallback(std::function<bool(const std::string&, const std::vector<uint8_t>&)> callback);

  /**
   * @brief Set security mode
   * @param secure true to enable secure mode
   */
  void SetSecureMode(bool secure);

  /**
   * @brief Set maximum transfer size
   * @param size Maximum transfer size (bytes)
   */
  void SetMaxTransferSize(size_t size);

  /**
   * @brief Set timeout value
   * @param seconds Timeout (seconds)
   */
  void SetTimeout(int seconds);

 private:
  friend class internal::TftpServerImpl;
  std::unique_ptr<internal::TftpServerImpl> impl_;
};

/**
 * @class TftpClient
 * @brief Class that provides TFTP client functionality (libcurl-based)
 */
class TFTP_EXPORT TftpClient {
 public:
  /**
   * @brief Constructor
   */
  TftpClient();

  /**
   * @brief Destructor
   */
  ~TftpClient();

  /**
   * @brief Download a file
   * @param host Hostname or IP address
   * @param filename Filename
   * @param output_buffer Output buffer
   * @param port Port number (default is 69)
   * @return true if successful, false if failed
   */
  bool DownloadFile(const std::string& host, const std::string& filename, 
                    std::vector<uint8_t>& output_buffer, uint16_t port = kDefaultTftpPort);

  /**
   * @brief Upload a file
   * @param host Hostname or IP address
   * @param filename Filename
   * @param data Data buffer
   * @param port Port number (default is 69)
   * @return true if successful, false if failed
   */
  bool UploadFile(const std::string& host, const std::string& filename, 
                  const std::vector<uint8_t>& data, uint16_t port = kDefaultTftpPort);

  /**
   * @brief Set timeout value
   * @param seconds Timeout (seconds)
   */
  void SetTimeout(int seconds);

  /**
   * @brief Set transfer mode
   * @param mode Transfer mode
   */
  void SetTransferMode(TransferMode mode);

  /**
   * @brief Get last error message
   * @return Error message
   */
  std::string GetLastError() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace tftpserver

#endif // TFTP_SERVER_H_ 