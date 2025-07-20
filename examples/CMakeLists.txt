# Example executables for TFTP Server

# Server example
add_executable(server_example server_example.cpp)
target_link_libraries(server_example PRIVATE tftpserver_lib)
target_include_directories(server_example PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Set C++17 standard
target_compile_features(server_example PRIVATE cxx_std_17)

# Platform-specific settings
if(WIN32)
    target_compile_definitions(server_example PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(server_example PRIVATE ws2_32)
endif()

# Install example
install(TARGETS server_example
    RUNTIME DESTINATION bin/examples
) 