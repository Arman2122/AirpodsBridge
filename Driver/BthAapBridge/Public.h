// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

//
// Shared kernel-mode / user-mode contract for the `BthAapBridge` KMDF Bluetooth L2CAP
// profile driver.
//
// Both `Driver/BthAapBridge` (C, KMDF) and `Source/Core/AirPodsTransport_win.cpp` (C++20,
// user-mode Win32) include this file so GUIDs, IOCTL codes, and structures never drift.
//

#ifdef _KERNEL_MODE
    #include <ntddk.h>
    #include <guiddef.h>
#else
    #include <stdint.h>
    #include <windows.h>
    #include <winioctl.h>
    #include <guiddef.h>
#endif

// {6A7C1D42-5E91-4B8F-9C23-18F4A20D7E51}
DEFINE_GUID(
    GUID_DEVINTERFACE_APB_AAP, 0x6a7c1d42, 0x5e91, 0x4b8f, 0x9c, 0x23, 0x18, 0xf4, 0xa2, 0x0d, 0x7e,
    0x51);

// {74EC2172-0BAD-4D01-8F77-997B2BE0722A} -- Apple Accessory Protocol (AAP) SDP Service Class
DEFINE_GUID(
    GUID_APB_AAP_SERVICE_CLASS, 0x74ec2172, 0x0bad, 0x4d01, 0x8f, 0x77, 0x99, 0x7b, 0x2b, 0xe0,
    0x72, 0x2a);

#define APB_AAP_DEFAULT_PSM 0x1001u
#define APB_AAP_MAX_PACKET_SIZE 1024u
#define APB_AAP_DEFAULT_LOW_LATENCY_US 10000u

#ifndef FILE_DEVICE_BLUETOOTH
    #define FILE_DEVICE_BLUETOOTH 0x00000041u
#endif

#define IOCTL_APB_AAP_GET_INFO                                                                     \
    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x801, METHOD_BUFFERED, FILE_READ_ACCESS)

#define IOCTL_APB_AAP_CONNECT                                                                      \
    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x802, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_APB_AAP_DISCONNECT                                                                   \
    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x803, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define IOCTL_APB_AAP_SET_LOW_LATENCY                                                              \
    CTL_CODE(FILE_DEVICE_BLUETOOTH, 0x804, METHOD_BUFFERED, FILE_ANY_ACCESS)

typedef struct _APB_AAP_DEVICE_INFO {
    UINT64 BluetoothAddress;
    UINT16 L2capPsm;
    UINT16 MaxMtu;
    UINT8 IsChannelOpen;
    UINT8 LowLatencyMode;
    UINT8 Reserved[2];
} APB_AAP_DEVICE_INFO, *PAPB_AAP_DEVICE_INFO;

typedef struct _APB_AAP_LOW_LATENCY_CONFIG {
    UINT8 Enable;
    UINT8 Reserved[3];
    UINT32 TargetLatencyUs;
} APB_AAP_LOW_LATENCY_CONFIG, *PAPB_AAP_LOW_LATENCY_CONFIG;
