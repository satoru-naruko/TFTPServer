/**
 * @file tftp_packet.h
 * @brief TFTP packet definition and operations
 */

#ifndef TFTP_PACKET_H_
#define TFTP_PACKET_H_

#include "tftp/tftp_common.h"
#include <vector>
#include <string>
#include <memory>
#include <unordered_map>

namespace tftpserver {

/**
 * @class TftpPacket
 * @brief Class for generating and parsing TFTP packets
 */
class TFTP_EXPORT TftpPacket {
public:
    TftpPacket() = default;
    ~TftpPacket() = default;

    // Get packet type
    OpCode GetOpCode() const { return op_code_; }
    uint16_t GetBlockNumber() const { return block_number_; }
    ErrorCode GetErrorCode() const { return error_code_; }
    const std::string& GetFilename() const { return filename_; }
    TransferMode GetMode() const { return mode_; }
    const std::vector<uint8_t>& GetData() const { return data_; }
    const std::string& GetErrorMessage() const { return error_message_; }

    // TFTP option processing
    bool HasOption(const std::string& option_name) const;
    std::string GetOption(const std::string& option_name) const;
    void SetOption(const std::string& option_name, const std::string& option_value);
    const std::unordered_map<std::string, std::string>& GetOptions() const { return options_; }

    // Packet serialization/deserialization
    // NOTE: TFTP packets use network byte order (big-endian) for all multi-byte fields
    std::vector<uint8_t> Serialize() const;
    bool Deserialize(const std::vector<uint8_t>& data);

    // Packet creation
    static TftpPacket CreateReadRequest(const std::string& filename, TransferMode mode);
    static TftpPacket CreateWriteRequest(const std::string& filename, TransferMode mode);
    static TftpPacket CreateData(uint16_t block_number, const std::vector<uint8_t>& data);
    static TftpPacket CreateAck(uint16_t block_number);
    static TftpPacket CreateError(ErrorCode code, const std::string& message);
    static TftpPacket CreateOACK(const std::unordered_map<std::string, std::string>& options);

    // Packet parsing
    static std::unique_ptr<TftpPacket> Parse(const std::vector<uint8_t>& data);

private:
    OpCode op_code_ = OpCode::kReadRequest;
    uint16_t block_number_ = 0;
    ErrorCode error_code_ = ErrorCode::kNotDefined;
    std::string filename_;
    TransferMode mode_ = TransferMode::kOctet;
    std::vector<uint8_t> data_;
    std::string error_message_;
    std::unordered_map<std::string, std::string> options_;  // TFTP options
};

} // namespace tftpserver

#endif // TFTP_PACKET_H_ 