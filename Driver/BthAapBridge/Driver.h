// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <bthdef.h>
#include <bthguid.h>
#include <bthioctl.h>
#include <bthddi.h>

#include "Public.h"

#define APB_POOL_TAG 'pAbA'
#define APB_RX_RING_CAPACITY 16u

typedef struct _APB_RX_SLOT {
    ULONG Length;
    UCHAR Data[APB_AAP_MAX_PACKET_SIZE];
} APB_RX_SLOT, *PAPB_RX_SLOT;

typedef struct _APB_DEVICE_CONTEXT {
    WDFDEVICE Device;
    WDFIOTARGET IoTarget;
    WDFQUEUE DefaultQueue;
    WDFQUEUE PendingReadsQueue;
    WDFWORKITEM RecvWorkItem;
    WDFWORKITEM DisconnectWorkItem;

    BTH_PROFILE_DRIVER_INTERFACE ProfileInterface;
    BOOLEAN HasProfileInterface;

    BTH_ADDR RemoteAddress;
    USHORT L2capPsm;
    USHORT NegotiatedMtu;

    WDFSPINLOCK StateLock;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
    BOOLEAN ChannelOpen;
    BOOLEAN LowLatencyMode;
    ULONG TargetLatencyUs;
    LONG PendingRecvCount;

    APB_RX_SLOT RxRing[APB_RX_RING_CAPACITY];
    ULONG RxHead;
    ULONG RxTail;
    ULONG RxCount;
} APB_DEVICE_CONTEXT, *PAPB_DEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(APB_DEVICE_CONTEXT, ApbGetDeviceContext)

NTSTATUS
ApbL2capOpenChannel(_In_ PAPB_DEVICE_CONTEXT Context);

VOID
ApbL2capCloseChannel(_In_ PAPB_DEVICE_CONTEXT Context);

NTSTATUS
ApbL2capSendPacket(
    _In_ PAPB_DEVICE_CONTEXT Context,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length);

VOID
ApbCompleteOrQueueInboundPacket(
    _In_ PAPB_DEVICE_CONTEXT Context,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length);

VOID
ApbDrainPendingReads(
    _In_ PAPB_DEVICE_CONTEXT Context,
    _In_ NTSTATUS CompletionStatus);

EVT_WDF_WORKITEM ApbEvtRecvWorkItem;
EVT_WDF_WORKITEM ApbEvtDisconnectWorkItem;
