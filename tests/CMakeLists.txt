# CMakeLists.txt for TFTP Server Tests

# Find GoogleTest package
find_package(GTest REQUIRED)

# Collect test source files
set(TEST_SOURCES
    tftp_server_test.cpp
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