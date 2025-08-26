# CMakeLists.txt for TFTP Server Tests

# Find GoogleTest package
find_package(GTest REQUIRED)

# Check for curl availability for integration tests
find_program(CURL_EXECUTABLE curl)
if(CURL_EXECUTABLE)
    message(STATUS "Found curl: ${CURL_EXECUTABLE}")
    message(STATUS "Curl integration tests will be enabled")
else()
    message(WARNING "curl not found - curl integration tests may fail")
    message(WARNING "Please install curl to run TFTP integration tests")
endif()

# Collect test source files
set(TEST_SOURCES
    tftp_server_test.cpp
    tftp_curl_integration_test.cpp
    tftp_curl_performance_test.cpp
    tftp_curl_security_test.cpp
    tftp_server_security_test.cpp
    tftp_packet_security_test.cpp
)

# Create test executable
add_executable(tftpserver_tests ${TEST_SOURCES})

# Set test properties
set_target_properties(tftpserver_tests PROPERTIES
    OUTPUT_NAME "tftpserver_tests"
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
)

# Configure include directories
target_include_directories(tftpserver_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
)

# Configure link libraries
target_link_libraries(tftpserver_tests
    PRIVATE
        tftpserver_lib
        GTest::gtest
        GTest::gtest_main
        GTest::gmock
        GTest::gmock_main
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(tftpserver_tests PRIVATE
        TFTPSERVER_EXPORTS
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(tftpserver_tests PRIVATE ws2_32)
else()
    # Settings for Linux/Unix
    find_package(Threads REQUIRED)
    target_link_libraries(tftpserver_tests PRIVATE Threads::Threads)
endif()

# Compiler-specific settings
if(MSVC)
    target_compile_options(tftpserver_tests PRIVATE /W4 /permissive-)
    # Enable console output during test execution
    target_compile_definitions(tftpserver_tests PRIVATE _CONSOLE)
else()
    target_compile_options(tftpserver_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Enable C++17 features
target_compile_features(tftpserver_tests PRIVATE cxx_std_17)

# Debug information settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(tftpserver_tests PRIVATE 
        DEBUG
        _DEBUG
    )
endif()

# Enable Google Test discovery
include(GoogleTest)

# Automatic test discovery and registration with CTest
gtest_discover_tests(tftpserver_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    DISCOVERY_TIMEOUT 60
    XML_OUTPUT_DIR "${CMAKE_BINARY_DIR}/test_results"
)

# Add specific curl integration test suites
add_test(
    NAME CurlTftpIntegrationTests
    COMMAND tftpserver_tests --gtest_filter="CurlTftpTest.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME CurlTftpPerformanceTests
    COMMAND tftpserver_tests --gtest_filter="CurlTftpPerformanceTest.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME CurlTftpSecurityTests
    COMMAND tftpserver_tests --gtest_filter="CurlTftpSecurityTest.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME TftpServerSecurityTests
    COMMAND tftpserver_tests --gtest_filter="TftpServerSecurityTest.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_test(
    NAME TftpPacketSecurityTests
    COMMAND tftpserver_tests --gtest_filter="TftpPacketSecurityTest.*"
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Set properties for curl integration tests
set_tests_properties(CurlTftpIntegrationTests PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}"
    TIMEOUT 600  # Extended timeout for curl integration tests
    LABELS "integration;curl;tftp"
)

set_tests_properties(CurlTftpPerformanceTests PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}"
    TIMEOUT 900  # Extended timeout for performance tests
    LABELS "performance;curl;tftp;benchmark"
)

set_tests_properties(CurlTftpSecurityTests PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}"
    TIMEOUT 300  # Timeout for security tests
    LABELS "security;curl;tftp"
)

set_tests_properties(TftpServerSecurityTests PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}"
    TIMEOUT 300  # Timeout for comprehensive security tests
    LABELS "security;tftp;comprehensive"
)

set_tests_properties(TftpPacketSecurityTests PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}"
    TIMEOUT 180  # Timeout for packet deserialization security tests
    LABELS "security;tftp;packet;deserialization"
)

# Manually add tests (fallback)
add_test(
    NAME TftpServerAllTests
    COMMAND tftpserver_tests
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# Set environment variables for test execution
set_tests_properties(TftpServerAllTests PROPERTIES
    ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}"
    TIMEOUT 300
)

# Coverage report generation (optional)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(tftpserver_tests PRIVATE --coverage)
    target_link_libraries(tftpserver_tests PRIVATE --coverage)
endif()

# Custom command to create test file directory
add_custom_command(TARGET tftpserver_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/test_files"
    COMMENT "Creating test files directory"
)

# Copy test files before test execution (if needed)
if(EXISTS "${CMAKE_SOURCE_DIR}/test_data")
    add_custom_command(TARGET tftpserver_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/test_data" "${CMAKE_BINARY_DIR}/test_files"
        COMMENT "Copying test data files"
    )
endif()

# Property settings for Visual Studio
if(MSVC)
    set_property(TARGET tftpserver_tests PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
    set_property(TARGET tftpserver_tests PROPERTY VS_DEBUGGER_ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin;$ENV{PATH}")
endif() 