# TFTP Server Library Source Files

# Collect library source files
set(TFTPSERVER_SOURCES
    # Main source files
    tftp_server.cpp
    tftp_packet.cpp
    tftp_util.cpp
    tftp_logger.cpp
    
    # Internal implementation files
    internal/tftp_server_impl.cpp
    internal/tftp_thread_pool.cpp
    # internal/tftp_client_impl.cpp  # Disabled as not used
    # internal/tftp_curl_wrapper_impl.cpp  # Temporarily disabled (not used in tests)
    
    # Note: tftp/tftp_logger.cpp is excluded (fully implemented in src/tftp_logger.cpp)
    # tftp/tftp_logger.cpp
)

# Collect library header files
set(TFTPSERVER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/tftp/tftp_server.h
    ${CMAKE_SOURCE_DIR}/include/tftp/tftp_packet.h
    ${CMAKE_SOURCE_DIR}/include/tftp/tftp_util.h
    ${CMAKE_SOURCE_DIR}/include/tftp/tftp_logger.h
    ${CMAKE_SOURCE_DIR}/include/tftp/tftp_common.h
    
    # Internal headers
    internal/tftp_server_impl.h
    internal/tftp_thread_pool.h
)

# Create TFTP server library
add_library(tftpserver_lib STATIC ${TFTPSERVER_SOURCES} ${TFTPSERVER_HEADERS})

# Set library properties
set_target_properties(tftpserver_lib PROPERTIES
    OUTPUT_NAME "tftpserver"
    POSITION_INDEPENDENT_CODE ON
)

# Configure include directories
target_include_directories(tftpserver_lib
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(tftpserver_lib PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(tftpserver_lib PUBLIC ws2_32)
else()
    # Settings for Linux/Unix
    find_package(Threads REQUIRED)
    target_link_libraries(tftpserver_lib PUBLIC Threads::Threads)
endif()

# Compiler-specific settings
if(MSVC)
    target_compile_options(tftpserver_lib PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(tftpserver_lib PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Format settings compliant with Google C++ Style Guide
target_compile_features(tftpserver_lib PUBLIC cxx_std_17)

# Library export symbol settings
if(BUILD_SHARED_LIBS)
    target_compile_definitions(tftpserver_lib PRIVATE TFTP_EXPORTS)
    target_compile_definitions(tftpserver_lib INTERFACE TFTP_IMPORTS)
endif()

# Debug information settings
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(tftpserver_lib PRIVATE 
        DEBUG
        _DEBUG
    )
endif() 