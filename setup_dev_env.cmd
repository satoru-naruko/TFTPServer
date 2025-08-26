@echo off
chcp 65001 >nul 2>&1
echo TFTPServer Development Environment Setup...

:: Set vcpkg path
set VCPKG_ROOT=C:\vcpkg
set PATH=%PATH%;%VCPKG_ROOT%

:: Set CMake toolchain file
set CMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

:: Display environment variables
echo.
echo === Environment Variables ===
echo VCPKG_ROOT=%VCPKG_ROOT%
echo CMAKE_TOOLCHAIN_FILE=%CMAKE_TOOLCHAIN_FILE%
echo.

:: Check if vcpkg is available
vcpkg version >nul 2>&1
if %ERRORLEVEL% == 0 (
    echo ✅ vcpkg is available
    vcpkg version
) else (
    echo ❌ vcpkg not found
    echo Path: %VCPKG_ROOT%
)

echo.
echo === Available Commands ===
echo vcpkg install        - Install dependencies
echo cmake_configure      - Configure CMake (alias)
echo cmake_build         - Build with CMake (alias)
echo cmake_test          - Run tests (alias)
echo.

:: Set CMake command aliases
doskey cmake_configure=cd build ^& cmake ..
doskey cmake_build=cd build ^& cmake --build . --config Debug
doskey cmake_test=cd build ^& ctest -C Debug --output-on-failure

echo Development environment setup complete!
echo Run this script every time you open a new terminal, or execute 'setup_dev_env.cmd'. 