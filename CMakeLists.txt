cmake_minimum_required(VERSION 3.20)

# プロジェクト設定
project(TFTPServer 
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "A high-performance TFTP server implementation in C++17"
)

# C++17標準を使用
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ビルドタイプの設定
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# vcpkg設定
if(DEFINED CMAKE_TOOLCHAIN_FILE)
    message(STATUS "Using vcpkg toolchain file: ${CMAKE_TOOLCHAIN_FILE}")
else()
    message(WARNING "CMAKE_TOOLCHAIN_FILE is not set. Please set it to vcpkg.cmake")
endif()

# 出力ディレクトリの設定
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# マルチコンフィグジェネレータ（Visual Studio等）向けの設定
foreach(OUTPUTCONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${OUTPUTCONFIG} OUTPUTCONFIG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/${OUTPUTCONFIG}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/${OUTPUTCONFIG}/lib")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${OUTPUTCONFIG} "${CMAKE_BINARY_DIR}/${OUTPUTCONFIG}/lib")
endforeach()

# インクルードディレクトリの設定
include_directories(${CMAKE_SOURCE_DIR}/include)

# コンパイラ固有の設定
if(MSVC)
    # MSVC向けの設定
    add_compile_options(/W4 /permissive-)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _WIN32_WINNT=0x0601  # Windows 7以降
        _CRT_SECURE_NO_WARNINGS
    )
    # Windows Socket2ライブラリをリンク
    link_libraries(ws2_32)
else()
    # GCC/Clang向けの設定
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Google C++ Style Guideに基づくclang-format設定を確認
find_program(CLANG_FORMAT_EXECUTABLE clang-format)
if(CLANG_FORMAT_EXECUTABLE)
    message(STATUS "Found clang-format: ${CLANG_FORMAT_EXECUTABLE}")
endif()

# サブディレクトリの追加
add_subdirectory(src)

# サンプルのオプション（デフォルトON）
option(BUILD_EXAMPLES "Build examples" ON)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# テストのオプション（デフォルトON）
option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# インストール設定
install(DIRECTORY include/ DESTINATION include)
install(TARGETS tftpserver_lib
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
) 