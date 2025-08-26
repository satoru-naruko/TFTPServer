#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <tftp/tftp_packet.h>
#include <tftp/tftp_common.h>
#include <tftp/tftp_logger.h>

#include <vector>
#include <string>
#include <cstring>
#include <random>
#include <algorithm>
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

using namespace tftpserver;

/**
 * @class TftpPacketSecurityTest
 * @brief Comprehensive security tests for TFTP packet deserialization
 * 
 * This test suite verifies that buffer overflow vulnerability fixes are working correctly
 * and that all malicious packet deserialization attempts are handled safely.
 */
class TftpPacketSecurityTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Initialize random number generator for creating malicious payloads
        rng_.seed(std::random_device{}());
    }

    /**
     * Create a raw packet with opcode and custom payload
     */
    std::vector<uint8_t> CreateRawPacket(uint16_t opcode, const std::vector<uint8_t>& payload = {}) {
        std::vector<uint8_t> packet(sizeof(uint16_t) + payload.size());
        
        // Set opcode in network byte order
        uint16_t opcode_network = htons(opcode);
        std::memcpy(packet.data(), &opcode_network, sizeof(uint16_t));
        
        // Copy payload
        if (!payload.empty()) {
            std::copy(payload.begin(), payload.end(), packet.begin() + sizeof(uint16_t));
        }
        
        return packet;
    }

    /**
     * Create a string without null terminator (for buffer overflow tests)
     */
    std::string CreateOversizedString(size_t length, char fill_char = 'A') {
        return std::string(length, fill_char);
    }

    /**
     * Create a payload with specified string and no null terminator
     */
    std::vector<uint8_t> CreateStringPayload(const std::string& str, bool add_null = false) {
        std::vector<uint8_t> payload(str.begin(), str.end());
        if (add_null) {
            payload.push_back('\0');
        }
        return payload;
    }

    /**
     * Create a payload with multiple strings (for RRQ/WRQ testing)
     */
    std::vector<uint8_t> CreateMultiStringPayload(const std::vector<std::string>& strings, 
                                                  bool add_nulls = true) {
        std::vector<uint8_t> payload;
        for (const auto& str : strings) {
            payload.insert(payload.end(), str.begin(), str.end());
            if (add_nulls) {
                payload.push_back('\0');
            }
        }
        return payload;
    }

    /**
     * Create a random binary payload
     */
    std::vector<uint8_t> CreateRandomPayload(size_t length) {
        std::vector<uint8_t> payload(length);
        std::generate(payload.begin(), payload.end(), 
                     [this]() { return static_cast<uint8_t>(rng_() % 256); });
        return payload;
    }

    /**
     * Test that packet deserialization fails safely
     */
    void TestDeserializationFailure(const std::vector<uint8_t>& malicious_packet, 
                                   const std::string& test_description) {
        TftpPacket packet;
        bool result = packet.Deserialize(malicious_packet);
        
        EXPECT_FALSE(result) << "Deserialization should fail for: " << test_description
                           << " (packet size: " << malicious_packet.size() << ")";
        
        // Verify packet state remains consistent after failed deserialization
        EXPECT_EQ(packet.GetFilename(), "") << "Filename should remain empty after failed deserialization";
        EXPECT_EQ(packet.GetErrorMessage(), "") << "Error message should remain empty after failed deserialization";
        EXPECT_TRUE(packet.GetOptions().empty()) << "Options should remain empty after failed deserialization";
    }

    /**
     * Test that packet deserialization succeeds as expected
     */
    void TestDeserializationSuccess(const std::vector<uint8_t>& valid_packet,
                                   const std::string& test_description) {
        TftpPacket packet;
        bool result = packet.Deserialize(valid_packet);
        
        EXPECT_TRUE(result) << "Deserialization should succeed for: " << test_description;
    }

private:
    std::mt19937 rng_;
};

// ============================================================================
// BUFFER OVERFLOW ATTACK TESTS
// ============================================================================

TEST_F(TftpPacketSecurityTest, BufferOverflow_OversizedFilename) {
    // Test oversized filename in RRQ packet (> kMaxFilenameLength = 255)
    std::string oversized_filename = CreateOversizedString(300);  // 300 > 255
    std::vector<uint8_t> payload = CreateMultiStringPayload({oversized_filename, "octet"});
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "RRQ with oversized filename (300 chars)");
}

TEST_F(TftpPacketSecurityTest, BufferOverflow_ExtremelyLongFilename) {
    // Test extremely long filename (> 1000 chars)
    std::string extreme_filename = CreateOversizedString(1024);
    std::vector<uint8_t> payload = CreateMultiStringPayload({extreme_filename, "octet"});
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "RRQ with extremely long filename (1024 chars)");
}

TEST_F(TftpPacketSecurityTest, BufferOverflow_OversizedOptionName) {
    // Test oversized option name (> kMaxOptionNameLength = 64)
    std::string oversized_option_name = CreateOversizedString(100);  // 100 > 64
    std::vector<uint8_t> payload = CreateMultiStringPayload({
        "test_file.txt", "octet", oversized_option_name, "512"
    });
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "RRQ with oversized option name (100 chars)");
}

TEST_F(TftpPacketSecurityTest, BufferOverflow_OversizedOptionValue) {
    // Test oversized option value (> kMaxOptionValueLength = 64)
    std::string oversized_option_value = CreateOversizedString(100);  // 100 > 64
    std::vector<uint8_t> payload = CreateMultiStringPayload({
        "test_file.txt", "octet", "blksize", oversized_option_value
    });
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "RRQ with oversized option value (100 chars)");
}

TEST_F(TftpPacketSecurityTest, BufferOverflow_OversizedErrorMessage) {
    // Test oversized error message (> kMaxErrorMessageLength = 255)
    std::string oversized_error = CreateOversizedString(300);  // 300 > 255
    
    std::vector<uint8_t> payload;
    // Add error code (network byte order)
    uint16_t error_code = htons(static_cast<uint16_t>(ErrorCode::kNotDefined));
    payload.resize(sizeof(uint16_t));
    std::memcpy(payload.data(), &error_code, sizeof(uint16_t));
    
    // Add oversized error message
    payload.insert(payload.end(), oversized_error.begin(), oversized_error.end());
    payload.push_back('\0');  // Null terminator
    
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kError), payload);
    
    TestDeserializationFailure(malicious_packet, "ERROR with oversized error message (300 chars)");
}

TEST_F(TftpPacketSecurityTest, BufferOverflow_MissingNullTerminator) {
    // Test filename without null terminator (should cause buffer overflow)
    std::string filename_no_null = "test_filename_without_null_terminator";
    std::vector<uint8_t> payload = CreateStringPayload(filename_no_null, false);  // No null terminator
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "RRQ with missing null terminator");
}

TEST_F(TftpPacketSecurityTest, BufferOverflow_MultipleStringsNoNullTerminators) {
    // Test multiple strings without null terminators
    std::vector<uint8_t> payload = CreateMultiStringPayload({
        "filename", "mode", "option1", "value1"
    }, false);  // No null terminators
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "RRQ with multiple strings missing null terminators");
}

// ============================================================================
// PACKET SIZE VALIDATION TESTS
// ============================================================================

TEST_F(TftpPacketSecurityTest, PacketSize_TooSmall) {
    // Test packet smaller than minimum size (< kMinPacketSize = 4)
    std::vector<uint8_t> tiny_packet = {0x00, 0x01};  // Only 2 bytes
    
    TestDeserializationFailure(tiny_packet, "Packet too small (2 bytes)");
}

TEST_F(TftpPacketSecurityTest, PacketSize_EmptyPacket) {
    // Test completely empty packet
    std::vector<uint8_t> empty_packet;
    
    TestDeserializationFailure(empty_packet, "Empty packet");
}

TEST_F(TftpPacketSecurityTest, PacketSize_TooLarge) {
    // Test packet larger than maximum size (> kMaxPacketSize = 516)
    std::vector<uint8_t> huge_packet = CreateRandomPayload(600);  // 600 > 516
    // Set valid opcode at the beginning
    uint16_t opcode_network = htons(static_cast<uint16_t>(OpCode::kData));
    std::memcpy(huge_packet.data(), &opcode_network, sizeof(uint16_t));
    
    TestDeserializationFailure(huge_packet, "Packet too large (600 bytes)");
}

TEST_F(TftpPacketSecurityTest, PacketSize_ExtremelyLarge) {
    // Test extremely large packet
    std::vector<uint8_t> extreme_packet = CreateRandomPayload(10000);  // 10KB
    uint16_t opcode_network = htons(static_cast<uint16_t>(OpCode::kData));
    std::memcpy(extreme_packet.data(), &opcode_network, sizeof(uint16_t));
    
    TestDeserializationFailure(extreme_packet, "Extremely large packet (10KB)");
}

// ============================================================================
// MALFORMED PACKET TESTS
// ============================================================================

TEST_F(TftpPacketSecurityTest, Malformed_InvalidOpcode) {
    // Test invalid opcodes (outside 1-6 range)
    std::vector<uint16_t> invalid_opcodes = {0, 7, 8, 255, 65535};
    
    for (uint16_t opcode : invalid_opcodes) {
        std::vector<uint8_t> malicious_packet = CreateRawPacket(opcode);
        TestDeserializationFailure(malicious_packet, 
                                 "Invalid opcode: " + std::to_string(opcode));
    }
}

TEST_F(TftpPacketSecurityTest, Malformed_InvalidErrorCode) {
    // Test invalid error codes (> 7)
    std::vector<uint16_t> invalid_error_codes = {8, 15, 255, 65535};
    
    for (uint16_t error_code : invalid_error_codes) {
        std::vector<uint8_t> payload;
        // Add invalid error code (network byte order)
        uint16_t error_code_network = htons(error_code);
        payload.resize(sizeof(uint16_t));
        std::memcpy(payload.data(), &error_code_network, sizeof(uint16_t));
        
        // Add error message
        std::string error_msg = "Invalid error";
        payload.insert(payload.end(), error_msg.begin(), error_msg.end());
        payload.push_back('\0');
        
        std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kError), payload);
        
        TestDeserializationFailure(malicious_packet, 
                                 "Invalid error code: " + std::to_string(error_code));
    }
}

TEST_F(TftpPacketSecurityTest, Malformed_TruncatedPackets) {
    // Test various truncated packet scenarios
    
    // Truncated RRQ (missing mode)
    std::vector<uint8_t> payload = CreateStringPayload("filename.txt", true);  // Only filename, no mode
    std::vector<uint8_t> truncated_rrq = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    TestDeserializationFailure(truncated_rrq, "Truncated RRQ (missing mode)");
    
    // Truncated DATA (missing block number)
    std::vector<uint8_t> truncated_data = CreateRawPacket(static_cast<uint16_t>(OpCode::kData));  // No block number
    TestDeserializationFailure(truncated_data, "Truncated DATA (missing block number)");
    
    // Truncated ERROR (missing error code)
    std::vector<uint8_t> truncated_error = CreateRawPacket(static_cast<uint16_t>(OpCode::kError));  // No error code
    TestDeserializationFailure(truncated_error, "Truncated ERROR (missing error code)");
}

TEST_F(TftpPacketSecurityTest, Malformed_InconsistentPacketSizes) {
    // Test ACK packet with wrong size (should be exactly 4 bytes)
    std::vector<uint8_t> payload = {0x00, 0x01, 0xFF};  // Extra byte
    std::vector<uint8_t> wrong_size_ack = CreateRawPacket(static_cast<uint16_t>(OpCode::kAcknowledge), payload);
    TestDeserializationFailure(wrong_size_ack, "ACK with wrong size (5 bytes instead of 4)");
    
    // Test DATA packet with excessive payload (> kMaxDataSize = 512)
    std::vector<uint8_t> block_number = {0x00, 0x01};  // Block number 1
    std::vector<uint8_t> excessive_data = CreateRandomPayload(600);  // 600 > 512
    payload = block_number;
    payload.insert(payload.end(), excessive_data.begin(), excessive_data.end());
    std::vector<uint8_t> oversized_data = CreateRawPacket(static_cast<uint16_t>(OpCode::kData), payload);
    TestDeserializationFailure(oversized_data, "DATA with excessive payload (600 bytes)");
}

// ============================================================================
// RESOURCE EXHAUSTION TESTS
// ============================================================================

TEST_F(TftpPacketSecurityTest, ResourceExhaustion_TooManyOptions) {
    // Test packet with too many options (> kMaxOptionsCount = 16)
    std::vector<std::string> strings = {"test_file.txt", "octet"};
    
    // Add 20 options (exceeding the limit of 16)
    for (int i = 0; i < 20; ++i) {
        strings.push_back("option" + std::to_string(i));
        strings.push_back("value" + std::to_string(i));
    }
    
    std::vector<uint8_t> payload = CreateMultiStringPayload(strings);
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "RRQ with too many options (20 options)");
}

TEST_F(TftpPacketSecurityTest, ResourceExhaustion_MaxSizeMaliciousPacket) {
    // Test packet at maximum size with malicious content
    std::vector<uint8_t> payload(kMaxPacketSize - sizeof(uint16_t));  // Max payload size
    std::fill(payload.begin(), payload.end(), 'A');  // Fill with 'A' (no null terminators)
    
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationFailure(malicious_packet, "Max size packet with no null terminators");
}

TEST_F(TftpPacketSecurityTest, ResourceExhaustion_RepeatedMaliciousDeserialization) {
    // Test repeated deserialization of malicious packets (memory leak detection)
    std::string oversized_filename = CreateOversizedString(500);
    std::vector<uint8_t> payload = CreateMultiStringPayload({oversized_filename, "octet"});
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    // Attempt deserialization 1000 times
    for (int i = 0; i < 1000; ++i) {
        TftpPacket packet;
        bool result = packet.Deserialize(malicious_packet);
        EXPECT_FALSE(result) << "Deserialization should fail on iteration " << i;
    }
}

// ============================================================================
// EDGE CASE DESERIALIZATION TESTS
// ============================================================================

TEST_F(TftpPacketSecurityTest, EdgeCase_BoundaryStringLengths) {
    // Test strings at exact boundary lengths
    
    // Filename at maximum allowed length (255 chars)
    std::string max_filename = CreateOversizedString(kMaxFilenameLength);  // Exactly 255 chars
    std::vector<uint8_t> payload = CreateMultiStringPayload({max_filename, "octet"});
    std::vector<uint8_t> boundary_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    // This should succeed as it's within limits
    TestDeserializationSuccess(boundary_packet, "Filename at maximum length (255 chars)");
    
    // Option name at maximum allowed length (64 chars)
    std::string max_option_name = CreateOversizedString(kMaxOptionNameLength);  // Exactly 64 chars
    payload = CreateMultiStringPayload({"test.txt", "octet", max_option_name, "value"});
    boundary_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationSuccess(boundary_packet, "Option name at maximum length (64 chars)");
    
    // Option value at maximum allowed length (64 chars)
    std::string max_option_value = CreateOversizedString(kMaxOptionValueLength);  // Exactly 64 chars
    payload = CreateMultiStringPayload({"test.txt", "octet", "blksize", max_option_value});
    boundary_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TestDeserializationSuccess(boundary_packet, "Option value at maximum length (64 chars)");
}

TEST_F(TftpPacketSecurityTest, EdgeCase_BoundaryPacketSizes) {
    // Test packets at exact boundary sizes
    
    // Minimum valid packet size (4 bytes)
    std::vector<uint8_t> min_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kAcknowledge), {0x00, 0x01});
    TestDeserializationSuccess(min_packet, "Minimum valid packet size (4 bytes)");
    
    // Maximum valid packet size (516 bytes)
    std::vector<uint8_t> max_data(kMaxDataSize);  // 512 bytes of data
    std::fill(max_data.begin(), max_data.end(), 0x42);
    
    std::vector<uint8_t> payload = {0x00, 0x01};  // Block number
    payload.insert(payload.end(), max_data.begin(), max_data.end());
    
    std::vector<uint8_t> max_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kData), payload);
    EXPECT_EQ(max_packet.size(), kMaxPacketSize) << "Packet should be exactly maximum size";
    
    TestDeserializationSuccess(max_packet, "Maximum valid packet size (516 bytes)");
}

TEST_F(TftpPacketSecurityTest, EdgeCase_EmptyStrings) {
    // Test empty filename (should fail)
    std::vector<uint8_t> payload = CreateMultiStringPayload({"", "octet"});
    std::vector<uint8_t> empty_filename_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    TestDeserializationFailure(empty_filename_packet, "Empty filename");
    
    // Test empty mode (should fail)
    payload = CreateMultiStringPayload({"test.txt", ""});
    std::vector<uint8_t> empty_mode_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    TestDeserializationFailure(empty_mode_packet, "Empty mode");
    
    // Test empty error message (should succeed as it may be valid)
    payload.clear();
    uint16_t error_code = htons(static_cast<uint16_t>(ErrorCode::kNotDefined));
    payload.resize(sizeof(uint16_t));
    std::memcpy(payload.data(), &error_code, sizeof(uint16_t));
    payload.push_back('\0');  // Empty error message with null terminator
    
    std::vector<uint8_t> empty_error_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kError), payload);
    TestDeserializationSuccess(empty_error_packet, "Empty error message (may be valid)");
}

TEST_F(TftpPacketSecurityTest, EdgeCase_NullByteInjection) {
    // Test strings with embedded null bytes
    std::string filename_with_null = "file";
    filename_with_null.push_back('\0');
    filename_with_null += "injection.txt";
    
    std::vector<uint8_t> payload;
    payload.insert(payload.end(), filename_with_null.begin(), filename_with_null.end());
    payload.push_back('\0');  // Final null terminator
    payload.insert(payload.end(), {'o', 'c', 't', 'e', 't', '\0'});  // Mode
    
    std::vector<uint8_t> null_injection_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    // The packet might succeed but the filename should be truncated at first null
    TftpPacket packet;
    bool result = packet.Deserialize(null_injection_packet);
    if (result) {
        // If deserialization succeeds, filename should be truncated at first null
        EXPECT_EQ(packet.GetFilename(), "file") << "Filename should be truncated at first null byte";
    }
}

// ============================================================================
// PROTOCOL COMPLIANCE TESTS
// ============================================================================

TEST_F(TftpPacketSecurityTest, ProtocolCompliance_ValidPacketTypes) {
    // Test all valid TFTP packet types deserialize correctly
    
    // Valid RRQ
    std::vector<uint8_t> payload = CreateMultiStringPayload({"test.txt", "octet"});
    std::vector<uint8_t> rrq_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    TestDeserializationSuccess(rrq_packet, "Valid RRQ packet");
    
    // Valid WRQ
    std::vector<uint8_t> wrq_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kWriteRequest), payload);
    TestDeserializationSuccess(wrq_packet, "Valid WRQ packet");
    
    // Valid DATA
    payload = {0x00, 0x01, 'H', 'e', 'l', 'l', 'o'};  // Block 1 + "Hello"
    std::vector<uint8_t> data_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kData), payload);
    TestDeserializationSuccess(data_packet, "Valid DATA packet");
    
    // Valid ACK
    payload = {0x00, 0x01};  // Block 1
    std::vector<uint8_t> ack_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kAcknowledge), payload);
    TestDeserializationSuccess(ack_packet, "Valid ACK packet");
    
    // Valid ERROR
    payload.clear();
    uint16_t error_code = htons(static_cast<uint16_t>(ErrorCode::kFileNotFound));
    payload.resize(sizeof(uint16_t));
    std::memcpy(payload.data(), &error_code, sizeof(uint16_t));
    std::string error_msg = "File not found";
    payload.insert(payload.end(), error_msg.begin(), error_msg.end());
    payload.push_back('\0');
    
    std::vector<uint8_t> error_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kError), payload);
    TestDeserializationSuccess(error_packet, "Valid ERROR packet");
    
    // Valid OACK
    payload = CreateMultiStringPayload({"blksize", "512", "timeout", "5"});
    std::vector<uint8_t> oack_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kOACK), payload);
    TestDeserializationSuccess(oack_packet, "Valid OACK packet");
}

TEST_F(TftpPacketSecurityTest, ProtocolCompliance_NetworkByteOrder) {
    // Test that network byte order handling is maintained
    
    // Test with high block number to verify byte order handling
    uint16_t high_block_number = 0x1234;  // In host byte order
    uint16_t block_network = htons(high_block_number);
    
    std::vector<uint8_t> payload;
    payload.resize(sizeof(uint16_t));
    std::memcpy(payload.data(), &block_network, sizeof(uint16_t));
    payload.insert(payload.end(), {'T', 'e', 's', 't'});  // Some data
    
    std::vector<uint8_t> data_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kData), payload);
    
    TftpPacket packet;
    bool result = packet.Deserialize(data_packet);
    EXPECT_TRUE(result) << "DATA packet with high block number should deserialize";
    
    if (result) {
        EXPECT_EQ(packet.GetBlockNumber(), high_block_number) 
            << "Block number should be correctly converted from network byte order";
    }
}

TEST_F(TftpPacketSecurityTest, ProtocolCompliance_ValidOptionCombinations) {
    // Test various valid option combinations
    
    // Single option
    std::vector<uint8_t> payload = CreateMultiStringPayload({"file.txt", "octet", "blksize", "1024"});
    std::vector<uint8_t> single_option_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    TestDeserializationSuccess(single_option_packet, "RRQ with single option");
    
    // Multiple options (within limit)
    payload = CreateMultiStringPayload({
        "file.txt", "octet",
        "blksize", "1024",
        "timeout", "10",
        "tsize", "0"
    });
    std::vector<uint8_t> multi_option_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    TestDeserializationSuccess(multi_option_packet, "RRQ with multiple options");
    
    // Maximum allowed options (16 options)
    std::vector<std::string> max_options = {"file.txt", "octet"};
    for (int i = 0; i < 16; ++i) {
        max_options.push_back("opt" + std::to_string(i));
        max_options.push_back("val" + std::to_string(i));
    }
    
    payload = CreateMultiStringPayload(max_options);
    std::vector<uint8_t> max_options_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    TestDeserializationSuccess(max_options_packet, "RRQ with maximum allowed options (16)");
}

// ============================================================================
// COMPREHENSIVE SECURITY VERIFICATION TESTS
// ============================================================================

TEST_F(TftpPacketSecurityTest, SecurityVerification_NoMemoryLeaks) {
    // Test that failed deserializations don't cause memory leaks
    std::vector<std::vector<uint8_t>> malicious_packets;
    
    // Create various malicious packets
    malicious_packets.push_back(CreateRawPacket(0));  // Invalid opcode
    malicious_packets.push_back(CreateRawPacket(999, CreateRandomPayload(1000)));  // Invalid opcode + large payload
    
    std::string huge_string = CreateOversizedString(2000);
    malicious_packets.push_back(CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), 
                                               CreateStringPayload(huge_string, false)));
    
    // Test each malicious packet multiple times
    for (const auto& malicious_packet : malicious_packets) {
        for (int i = 0; i < 100; ++i) {
            TftpPacket packet;
            bool result = packet.Deserialize(malicious_packet);
            EXPECT_FALSE(result) << "Malicious packet should fail deserialization on iteration " << i;
        }
    }
}

TEST_F(TftpPacketSecurityTest, SecurityVerification_StateConsistency) {
    // Verify that packet state remains consistent after failed deserialization
    std::string oversized_filename = CreateOversizedString(300);
    std::vector<uint8_t> payload = CreateMultiStringPayload({oversized_filename, "octet"});
    std::vector<uint8_t> malicious_packet = CreateRawPacket(static_cast<uint16_t>(OpCode::kReadRequest), payload);
    
    TftpPacket packet;
    
    // Set some initial state
    packet.SetOption("test_option", "test_value");
    
    // Attempt deserialization of malicious packet
    bool result = packet.Deserialize(malicious_packet);
    EXPECT_FALSE(result) << "Malicious packet deserialization should fail";
    
    // Verify state is reset/consistent
    EXPECT_EQ(packet.GetFilename(), "") << "Filename should be empty after failed deserialization";
    EXPECT_EQ(packet.GetErrorMessage(), "") << "Error message should be empty after failed deserialization";
    // Note: Options may or may not be cleared depending on implementation
}

TEST_F(TftpPacketSecurityTest, SecurityVerification_ExceptionSafety) {
    // Verify that deserialization handles exceptions safely
    std::vector<std::vector<uint8_t>> edge_case_packets;
    
    // Create various edge case packets that might cause exceptions
    edge_case_packets.push_back({});  // Empty
    edge_case_packets.push_back({0x00});  // Single byte
    edge_case_packets.push_back({0xFF, 0xFF});  // Invalid opcode
    edge_case_packets.push_back(CreateRandomPayload(3));  // Too small
    edge_case_packets.push_back(CreateRandomPayload(1000));  // Too large
    
    // Test that no exceptions are thrown
    for (const auto& packet_data : edge_case_packets) {
        TftpPacket packet;
        EXPECT_NO_THROW({
            bool result = packet.Deserialize(packet_data);
            // Should return false for malformed packets
            (void)result;  // Suppress unused variable warning
        }) << "Deserialization should not throw exceptions for malformed packets";
    }
}