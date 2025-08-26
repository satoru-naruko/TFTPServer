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

    // Read null-terminated string with comprehensive bounds checking
    std::string read_string(const std::vector<uint8_t>& data, size_t& offset, size_t max_length = kMaxStringLength) {
        std::string result;
        size_t start_offset = offset;
        
        // Validate offset is within bounds
        if (offset >= data.size()) {
            TFTP_ERROR("read_string: offset %zu exceeds data size %zu", offset, data.size());
            return result;  // Return empty string for invalid offset
        }
        
        // Read characters with multiple safety checks
        size_t chars_read = 0;
        while (offset < data.size() && data[offset] != 0 && chars_read < max_length) {
            result += static_cast<char>(data[offset++]);
            chars_read++;
        }
        
        // Validate string termination
        if (offset >= data.size()) {
            TFTP_ERROR("read_string: reached end of data without null terminator at offset %zu", offset);
            return std::string();  // Return empty string for unterminated string
        }
        
        // Check for maximum length exceeded
        if (chars_read >= max_length && data[offset] != 0) {
            TFTP_ERROR("read_string: string exceeds maximum length %zu at offset %zu", max_length, start_offset);
            return std::string();  // Return empty string for oversized string
        }
        
        // Skip null terminator safely
        if (offset < data.size() && data[offset] == 0) {
            ++offset;
        }
        
        // Validate final result
        if (result.length() > max_length) {
            TFTP_ERROR("read_string: final string length %zu exceeds maximum %zu", result.length(), max_length);
            return std::string();  // Return empty string for invalid result
        }
        
        // Log successful parsing with security info
        TFTP_INFO("read_string: start_offset=%zu, end_offset=%zu, string_length=%zu, max_allowed=%zu, result='%s'", 
                 start_offset, offset, result.length(), max_length, result.c_str());
        
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
            
            // opcode (convert from host byte order to network byte order)
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
            
            // opcode (convert from host byte order to network byte order)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // block number (convert from host byte order to network byte order)
            uint16_t block_number_network = htons(block_number_);
            std::memcpy(&result[2], &block_number_network, sizeof(uint16_t));
            
            // data
            std::copy(data_.begin(), data_.end(), result.begin() + 4);
            break;
        }
        case OpCode::kAcknowledge: {
            // ACK packet: opcode + block#
            result.resize(4);
            
            // opcode (convert from host byte order to network byte order)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // block number (convert from host byte order to network byte order)
            uint16_t block_number_network = htons(block_number_);
            std::memcpy(&result[2], &block_number_network, sizeof(uint16_t));
            break;
        }
        case OpCode::kError: {
            // ERROR packet: opcode + errorcode + errmsg + 0
            result.resize(4 + error_message_.length() + 1);
            
            // opcode (convert from host byte order to network byte order)
            uint16_t opcode_network = htons(static_cast<uint16_t>(op_code_));
            std::memcpy(&result[0], &opcode_network, sizeof(uint16_t));
            
            // error code (convert from host byte order to network byte order)
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
            
            // opcode (convert from host byte order to network byte order)
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
    // Comprehensive input validation
    if (data.empty()) {
        TFTP_ERROR("Empty packet data");
        return false;
    }
    
    if (data.size() < kMinPacketSize || data.size() > kMaxPacketSize) {
        TFTP_ERROR("Invalid packet size %zu (min=%zu, max=%zu)", data.size(), kMinPacketSize, kMaxPacketSize);
        return false;
    }
    
    if (data.size() < sizeof(uint16_t)) {
        TFTP_ERROR("Packet too small for opcode: size=%zu", data.size());
        return false;
    }
    
    // Detailed hex dump of the entire packet
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
    
    // opcode (convert from network byte order to host byte order) - with bounds checking
    uint16_t opcode_network;
    if (data.size() < sizeof(uint16_t)) {
        TFTP_ERROR("Insufficient data for opcode: size=%zu", data.size());
        return false;
    }
    std::memcpy(&opcode_network, &data[0], sizeof(uint16_t));
    op_code_ = static_cast<OpCode>(ntohs(opcode_network));
    
    // Validate opcode is in valid range
    uint16_t opcode_value = static_cast<uint16_t>(op_code_);
    if (opcode_value < 1 || opcode_value > 6) {
        TFTP_ERROR("Invalid opcode value: %u", opcode_value);
        return false;
    }
    
    size_t offset = 2;
    
    switch (op_code_) {
        case OpCode::kReadRequest:
        case OpCode::kWriteRequest: {
            // filename with length validation
            filename_ = read_string(data, offset, kMaxFilenameLength);
            if (filename_.empty()) {
                TFTP_ERROR("Empty or invalid filename");
                return false;
            }
            TFTP_INFO("Parsed filename: %s", filename_.c_str());
            
            // mode with bounds checking
            if (offset >= data.size()) {
                TFTP_ERROR("Packet too small for mode");
                return false;
            }
            
            std::string mode_str = read_string(data, offset, kMaxStringLength);
            if (mode_str.empty()) {
                TFTP_ERROR("Empty or invalid mode string");
                return false;
            }
            TFTP_INFO("Parsed mode: %s", mode_str.c_str());
            try {
                mode_ = string_to_mode(mode_str);
            } catch (const TftpException&) {
                // Catch exception object to resolve unused variable warning, but don't use it
                TFTP_ERROR("Invalid mode: %s", mode_str.c_str());
                return false;
            }
            
            // options (if present) with comprehensive validation
            options_.clear();
            size_t options_count = 0;
            TFTP_INFO("Checking for options, remaining bytes: %zu", data.size() - offset);
            
            while (offset < data.size() && options_count < kMaxOptionsCount) {
                std::string option_name = read_string(data, offset, kMaxOptionNameLength);
                if (option_name.empty()) {
                    TFTP_INFO("No more valid options found or invalid option name");
                    break;
                }
                
                if (offset >= data.size()) {
                    TFTP_ERROR("Missing option value for option: %s", option_name.c_str());
                    return false;
                }
                
                std::string option_value = read_string(data, offset, kMaxOptionValueLength);
                if (option_value.empty()) {
                    TFTP_ERROR("Empty or invalid option value for option: %s", option_name.c_str());
                    return false;
                }
                
                options_[option_name] = option_value;
                options_count++;
                TFTP_INFO("TFTP option: %s = %s", option_name.c_str(), option_value.c_str());
            }
            
            if (options_count >= kMaxOptionsCount && offset < data.size()) {
                TFTP_ERROR("Too many options in packet (max=%zu)", kMaxOptionsCount);
                return false;
            }
            
            TFTP_INFO("Total options parsed: %zu", options_.size());
            
            break;
        }
        case OpCode::kData: {
            if (data.size() < 4) {
                TFTP_ERROR("DATA packet too small: size=%zu", data.size());
                return false;
            }
            
            // block number (convert from network byte order to host byte order) with bounds checking
            uint16_t block_number_network;
            if (data.size() < 4) {
                TFTP_ERROR("Insufficient data for DATA block number: size=%zu", data.size());
                return false;
            }
            std::memcpy(&block_number_network, &data[2], sizeof(uint16_t));
            block_number_ = ntohs(block_number_network);
            
            // data with size validation
            size_t data_size = data.size() - 4;
            if (data_size > kMaxDataSize) {
                TFTP_ERROR("DATA payload too large: %zu bytes (max=%zu)", data_size, kMaxDataSize);
                return false;
            }
            data_.assign(data.begin() + 4, data.end());
            
            TFTP_INFO("DATA packet: block=%u, data_size=%zu", block_number_, data_.size());
            break;
        }
        case OpCode::kAcknowledge: {
            if (data.size() != 4) {
                TFTP_ERROR("ACK packet has incorrect size: %zu (expected: 4)", data.size());
                return false;
            }
            
            // block number (convert from network byte order to host byte order) with bounds checking
            uint16_t block_number_network;
            if (data.size() < 4) {
                TFTP_ERROR("Insufficient data for ACK block number: size=%zu", data.size());
                return false;
            }
            std::memcpy(&block_number_network, &data[2], sizeof(uint16_t));
            block_number_ = ntohs(block_number_network);
            
            TFTP_INFO("ACK packet: block=%u", block_number_);
            break;
        }
        case OpCode::kError: {
            if (data.size() < 5) {
                TFTP_ERROR("ERROR packet too small: size=%zu", data.size());
                return false;
            }
            
            // error code (convert from network byte order to host byte order) with bounds checking
            uint16_t error_code_network;
            if (data.size() < 4) {
                TFTP_ERROR("Insufficient data for ERROR code: size=%zu", data.size());
                return false;
            }
            std::memcpy(&error_code_network, &data[2], sizeof(uint16_t));
            uint16_t error_code_value = ntohs(error_code_network);
            
            // Validate error code is in valid range
            if (error_code_value > 7) {
                TFTP_ERROR("Invalid error code: %u", error_code_value);
                return false;
            }
            error_code_ = static_cast<ErrorCode>(error_code_value);
            
            // error message with length validation
            error_message_ = read_string(data, offset, kMaxErrorMessageLength);
            if (error_message_.empty()) {
                TFTP_WARN("Empty error message in ERROR packet");
                // Don't fail for empty error message as it may be valid
            }
            
            TFTP_INFO("ERROR packet: code=%u, message='%s'", error_code_value, error_message_.c_str());
            break;
        }
        case OpCode::kOACK: {
            // OACK packet: opcode + (option + 0 + value + 0)* with comprehensive validation
            options_.clear();
            size_t options_count = 0;
            TFTP_INFO("Parsing OACK packet, remaining bytes: %zu", data.size() - offset);
            
            while (offset < data.size() && options_count < kMaxOptionsCount) {
                std::string option_name = read_string(data, offset, kMaxOptionNameLength);
                if (option_name.empty()) {
                    TFTP_INFO("No more valid options found or invalid option name in OACK");
                    break;
                }
                
                if (offset >= data.size()) {
                    TFTP_ERROR("Missing option value for OACK option: %s", option_name.c_str());
                    return false;
                }
                
                std::string option_value = read_string(data, offset, kMaxOptionValueLength);
                if (option_value.empty()) {
                    TFTP_ERROR("Empty or invalid option value for OACK option: %s", option_name.c_str());
                    return false;
                }
                
                options_[option_name] = option_value;
                options_count++;
                TFTP_INFO("OACK option: %s = %s", option_name.c_str(), option_value.c_str());
            }
            
            if (options_count >= kMaxOptionsCount && offset < data.size()) {
                TFTP_ERROR("Too many options in OACK packet (max=%zu)", kMaxOptionsCount);
                return false;
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