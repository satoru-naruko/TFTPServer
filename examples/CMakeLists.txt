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

# Socket demo example  
add_executable(socket_demo socket_demo.cpp)
target_link_libraries(socket_demo PRIVATE tftpserver_lib)
target_include_directories(socket_demo PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_compile_features(socket_demo PRIVATE cxx_std_17)

if(WIN32)
    target_compile_definitions(socket_demo PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(socket_demo PRIVATE ws2_32)
endif()

# Install examples
install(TARGETS server_example socket_demo
    RUNTIME DESTINATION bin/examples
) 