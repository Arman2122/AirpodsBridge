# SPDX-FileCopyrightText: 2026 Arman2122
# SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

cmake_minimum_required(VERSION 3.20)

project(
    AirpodsBridgeService
    VERSION 2.0.0
    LANGUAGES CXX
    DESCRIPTION "Native 64-bit (x64/ARM64) No-Test-Mode Bluetooth HCI/L2CAP Proxy Service for AirpodsBridge"
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
# Link the MSVC runtime statically (/MT) so AirpodsBridgeService.exe runs from
# %ProgramFiles%\AirpodsBridge\RadioService with zero external DLL dependencies,
# even on a brand-new Windows x64 or ARM64 PC without the VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(APB_SERVICE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(APB_SERVICE_OUT_DIR "${CMAKE_BINARY_DIR}/Binary")

foreach (DIR_VAR
    CMAKE_RUNTIME_OUTPUT_DIRECTORY
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE
    CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE
    CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO)
    set(${DIR_VAR} "${APB_SERVICE_OUT_DIR}")
endforeach()

# Pure platform-free protocol & HCI/L2CAP MITM library (zero Windows/Qt dependencies).
add_library(
    apb_radio_protocol STATIC
    "Protocol/ServiceProtocol.cpp"
    "Protocol/WatchdogPolicy.cpp"
    "Protocol/UsbIpProtocol.cpp"
    "Protocol/HciProxyEngine.cpp"
    "Protocol/Uac1MicEmulator.cpp"
)

target_include_directories(apb_radio_protocol PUBLIC "${APB_SERVICE_ROOT}")

if (MSVC)
    target_compile_options(apb_radio_protocol PRIVATE /W4 /MP)
endif()

# Native x64 Windows Radio Proxy Service executable (zero external runtimes).
add_executable(
    AirpodsBridgeService
    "Win/ServiceLog.cpp"
    "Win/RegistryStore.cpp"
    "Win/RadioDriverManager.cpp"
    "Win/WinUsbRadio.cpp"
    "Win/UsbIpServer.cpp"
    "Win/RadioBridgeSession.cpp"
    "Win/MicBridgeSession.cpp"
    "Win/NamedPipeServer.cpp"
    "Win/RadioEngine.cpp"
    "Win/NativeSetup.cpp"
    "Win/ServiceMain.cpp"
)

target_link_libraries(
    AirpodsBridgeService
    PRIVATE
    apb_radio_protocol
    winusb
    setupapi
    newdev
    cfgmgr32
    ws2_32
    advapi32
    avrt
    winmm
    winhttp
    bcrypt
)

target_include_directories(AirpodsBridgeService PRIVATE "${APB_SERVICE_ROOT}")
target_compile_definitions(
    AirpodsBridgeService
    PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
)

if (MSVC)
    target_compile_options(AirpodsBridgeService PRIVATE /W4 /MP)
endif()
