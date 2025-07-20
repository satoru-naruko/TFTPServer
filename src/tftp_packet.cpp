#include "tftp/tftp_packet.h"
#include "tftp/tftp_logger.h"
#include <cstring>
#include <algorithm>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#endif

namespace tftpserver {

namespace {
    // Copy string with null terminator
    void copy_string(std::vector<uint8_t>& dest, size_t& offset, const std::string& src) {
        std::copy(src.begin(), src.end(), dest.begin() + offset);
        offset += src.length();
        dest[offset++] = 0;
    }

    // Read null-terminated string
    std::string read_string(const std::vector<uint8_t>& data, size_t& offset) {
        std::string result;
        size_t start_offset = offset;
        while (offset < data.size() && data[offset] != 0) {
            result += static_cast<char>(data[offset++]);
        }
        if (offset < data.size()) {
            ++offset;  // Skip null terminator
        }
        
        // パケット破損検出のためのログ追加
        TFTP_INFO("read_string: start_offset=%zu, end_offset=%zu, string_length=%zu, result='%s'", 
                 start_offset, offset, result.length(), result.c_str());
        
        return result;
    }

    // Convert TransferMode to string
    std::string mode_to_string(TransferMode mode) {
        switch (mode) {
            case TransferMode::kNetAscii:
                return "netascii";
            case TransferMode::kOctet:
                return "octet";
            case TransferMode::kMail:
                return "mail";
            default:
                throw TftpException("Unknown transfer mode");
        }
    }

    // Convert string to TransferMode
    TransferMode string_to_mode(const std::string& mode_str) {
        std::string lower_mode;
        lower_mode.resize(mode_str.size());
        std::transform(mode_str.begin(), mode_str.end(), lower_mode.begin(), 
                     [](unsigned char c) { return std::tolower(c); });

        if (lower_mode == "netascii") {
            return TransferMode::kNetAscii;
        } else if (lower_mode == "octet") {
            return TransferMode::kOctet;
        } else if (lower_mode == "mail") {
            return TransferMode::kMail;
        } else {
            throw TftpException("Invalid transfer mode: " + mode_str);
        }
    }
}

// TftpPacket static factory method implementations
TftpPacket TftpPacket::CreateReadRequest(const std::string& filename, TransferMode mode) {
    TftpPacket packet;
    packet.op_code_ = OpCode::kReadRequest;
    packet.filename_ = filename;
    packet.mode_ = mode;
    return packet;
}

TftpPacket TftpPacket::CreateWriteRequest(const std::string& filename, TransferMode mode) {
    TftpPacket packet;
    packet.op_code_ = OpCode::kWriteRequest;
    packet.filename_ = filename;
    packet.mode_ = mode;
    return packet;
}

TftpPacket TftpPacket::CreateData(uint16_t block_number, const std::vector<uint8_t>& data) {
    if (data.size() > kMaxDataSize) {
        throw TftpException("Data size exceeds maximum allowed size");
    }
    
    TftpPacket packet;
    packet.op_code_ = OpCode::kData;
    packet.block_number_ = block_number;
    packet.data_ = data;
    return packet;
}

TftpPacket TftpPacket::CreateAck(uint16_t block_number) {
    TftpPacket packet;
    packet.op_code_ = OpCode::kAcknowledge;
    packet.block_number_ = block_number;
    return packet;
}

TftpPacket TftpPacket::CreateError(ErrorCode code, const std::string& message) {
    TftpPacket packet;
    packet.op_code_ = OpCode::kError;
    packet.error_code_ = code;
    packet.error_message_ = message;
    return packet;
}

TftpPacket TftpPacket::CreateOACK(const std::unordered_map<std::string, std::string>& options) {
    TftpPacket packet;
    packet.op_code_ = OpCode::kOACK;
    packet.options_ = options;
    return packet;
}

bool TftpPacket::HasOption(const std::string& option_name) const {
    return options_.find(option_name) != options_.end();
}

std::string TftpPacket::GetOption(const std::string& option_name) const {
    auto it = options_.find(option_name);
    return (it != options_.end()) ? it->second : "";
}

void TftpPacket::SetOption(const std::string& option_name, const std::string& option_value) {
    options_[option_name] = option_value;
}

// Serialize implementation
// NOTE: TFTP uses network byte order (big-endian) for all multi-byte values
// We use htons() to convert from host byte order to network byte order
std::vector<uint8_t> TftpPacket::Serialize() const {
    std::vector<uint8_t> result;
    
    switch (op_code_) {
        case OpCode::kReadRequest:
        case OpCode::kWriteRequest: {
            // RRQ/WRQ packet: opcode + filename + 0 + mode + 0 + (options)
            std::string mode_str = mode_to_string(mode_);
            
            // Calculate total size: opcode(2) + filename + null + mode + null
            size_t total_size = 2 + filename_.length() + 1 + mode_str.length() + 1;
            
            // Add options size
            for (const auto& option : options_) {
                total_size += option.first.length() + 1 + option.second.length() + 1;
            }
            
            result.resize(total_size);
            
            // opcode (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // filename + null terminator
            size_t offset = 2;
            copy_string(result, offset, filename_);
            
            // mode + null terminator
            copy_string(result, offset, mode_str);
            
            // options
            for (const auto& option : options_) {
                copy_string(result, offset, option.first);   // option name
                copy_string(result, offset, option.second);  // option value
            }
            
            break;
        }
        case OpCode::kData: {
            // DATA packet: opcode + block# + data
            result.resize(4 + data_.size());
            
            // opcode (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // block number (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t block_number_network = htons(block_number_);
            std::memcpy(&result[2], &block_number_network, sizeof(uint16_t));
            
            // data
            std::copy(data_.begin(), data_.end(), result.begin() + 4);
            break;
        }
        case OpCode::kAcknowledge: {
            // ACK packet: opcode + block#
            result.resize(4);
            
            // opcode (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // block number (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t block_number_network = htons(block_number_);
            std::memcpy(&result[2], &block_number_network, sizeof(uint16_t));
            break;
        }
        case OpCode::kError: {
            // ERROR packet: opcode + errorcode + errmsg + 0
            result.resize(4 + error_message_.length() + 1);
            
            // opcode (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // error code (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t error_code_network = htons(static_cast<uint16_t>(error_code_));
            std::memcpy(&result[2], &error_code_network, sizeof(uint16_t));
            
            // error message + null terminator
            size_t offset = 4;
            copy_string(result, offset, error_message_);
            break;
        }
        case OpCode::kOACK: {
            // OACK packet: opcode + (option + 0 + value + 0)*
            // Calculate total size: opcode(2) + options
            size_t total_size = 2;
            for (const auto& option : options_) {
                total_size += option.first.length() + 1 + option.second.length() + 1;
            }
            
            result.resize(total_size);
            
            // opcode (ホストバイトオーダーからネットワークバイトオーダーに変換)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // options
            size_t offset = 2;
            for (const auto& option : options_) {
                copy_string(result, offset, option.first);   // option name
                copy_string(result, offset, option.second);  // option value
            }
            
            break;
        }
        default:
            TFTP_ERROR("Unknown operation code: %d", static_cast<int>(op_code_));
            return std::vector<uint8_t>();
    }
    
    return result;
}

// Deserialize implementation
// NOTE: TFTP uses network byte order (big-endian) for all multi-byte values
// We use ntohs() to convert from network byte order to host byte order
bool TftpPacket::Deserialize(const std::vector<uint8_t>& data) {
    if (data.size() < 2) {
        TFTP_ERROR("Packet size too small");
        return false;
    }
    
    // パケット全体の詳細な16進ダンプ
    TFTP_INFO("Full packet analysis: size=%zu bytes", data.size());
    std::string hex_dump;
    for (size_t i = 0; i < data.size(); ++i) {
        char buf[4];
        snprintf(buf, sizeof(buf), "%02X ", data[i]);
        hex_dump += buf;
        if ((i + 1) % 16 == 0) {
            hex_dump += "\n";
        }
    }
    TFTP_INFO("Complete hex dump:\n%s", hex_dump.c_str());
    
    // opcode (ネットワークバイトオーダーからホストバイトオーダーに変換)
    uint16_t opcode_network;
    std::memcpy(&opcode_network, &data[0], sizeof(uint16_t));
    op_code_ = static_cast<OpCode>(ntohs(opcode_network));
    
    size_t offset = 2;
    
    switch (op_code_) {
        case OpCode::kReadRequest:
        case OpCode::kWriteRequest: {
            // filename
            filename_ = read_string(data, offset);
            if (filename_.empty()) {
                TFTP_ERROR("Empty filename");
                return false;
            }
            TFTP_INFO("Parsed filename: %s", filename_.c_str());
            
            // mode
            if (offset >= data.size()) {
                TFTP_ERROR("Packet too small for mode");
                return false;
            }
            
            std::string mode_str = read_string(data, offset);
            TFTP_INFO("Parsed mode: %s", mode_str.c_str());
            try {
                mode_ = string_to_mode(mode_str);
            } catch (const TftpException&) {
                // 未使用変数警告を解消するために、例外オブジェクトを捕捉するが使用しない
                TFTP_ERROR("Invalid mode: %s", mode_str.c_str());
                return false;
            }
            
            // options (オプションがある場合)
            options_.clear();
            TFTP_INFO("Checking for options, remaining bytes: %zu", data.size() - offset);
            while (offset < data.size()) {
                std::string option_name = read_string(data, offset);
                if (option_name.empty() || offset >= data.size()) {
                    TFTP_INFO("No more options found or invalid option name");
                    break;
                }
                std::string option_value = read_string(data, offset);
                options_[option_name] = option_value;
                TFTP_INFO("TFTP option: %s = %s", option_name.c_str(), option_value.c_str());
            }
            TFTP_INFO("Total options parsed: %zu", options_.size());
            
            break;
        }
        case OpCode::kData: {
            if (data.size() < 4) {
                TFTP_ERROR("DATA packet too small");
                return false;
            }
            
            // block number（ネットワークバイトオーダーからホストバイトオーダーに変換）
            uint16_t block_number_network;
            std::memcpy(&block_number_network, &data[2], sizeof(uint16_t));
            block_number_ = ntohs(block_number_network);
            
            // data
            data_.assign(data.begin() + 4, data.end());
            break;
        }
        case OpCode::kAcknowledge: {
            if (data.size() != 4) {
                TFTP_ERROR("ACK packet has incorrect size");
                return false;
            }
            
            // block number（ネットワークバイトオーダーからホストバイトオーダーに変換）
            uint16_t block_number_network;
            std::memcpy(&block_number_network, &data[2], sizeof(uint16_t));
            block_number_ = ntohs(block_number_network);
            break;
        }
        case OpCode::kError: {
            if (data.size() < 5) {
                TFTP_ERROR("ERROR packet too small");
                return false;
            }
            
            // error code（ネットワークバイトオーダーからホストバイトオーダーに変換）
            uint16_t error_code_network;
            std::memcpy(&error_code_network, &data[2], sizeof(uint16_t));
            error_code_ = static_cast<ErrorCode>(ntohs(error_code_network));
            
            // error message
            error_message_ = read_string(data, offset);
            break;
        }
        case OpCode::kOACK: {
            // OACK packet: opcode + (option + 0 + value + 0)*
            options_.clear();
            TFTP_INFO("Parsing OACK packet, remaining bytes: %zu", data.size() - offset);
            
            while (offset < data.size()) {
                std::string option_name = read_string(data, offset);
                if (option_name.empty() || offset >= data.size()) {
                    TFTP_INFO("No more options found or invalid option name in OACK");
                    break;
                }
                std::string option_value = read_string(data, offset);
                options_[option_name] = option_value;
                TFTP_INFO("OACK option: %s = %s", option_name.c_str(), option_value.c_str());
            }
            TFTP_INFO("Total OACK options parsed: %zu", options_.size());
            break;
        }
        default:
            TFTP_ERROR("Unknown operation code: %d", static_cast<int>(op_code_));
            return false;
    }
    
    return true;
}

// TftpPacket::Parse function is not needed, Deserialize can be used instead

} // namespace tftpserver 