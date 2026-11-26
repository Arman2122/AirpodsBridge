// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Driver.h"

static NTSTATUS
ApbSubmitBrbSynchronously(_In_ PAPB_DEVICE_CONTEXT Context, _In_ PBRB Brb, _In_ ULONG TimeoutSeconds)
{
    WDFREQUEST request = NULL;
    WDF_REQUEST_SEND_OPTIONS options;
    IO_STACK_LOCATION stack;
    NTSTATUS status;

    status = WdfRequestCreate(WDF_NO_OBJECT_ATTRIBUTES, Context->IoTarget, &request);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    RtlZeroMemory(&stack, sizeof(stack));
    stack.MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    stack.Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_BTH_SUBMIT_BRB;
    stack.Parameters.Others.Argument1 = Brb;

    WdfRequestWdmFormatUsingStackLocation(request, &stack);

    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SYNCHRONOUS);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(TimeoutSeconds));

    if (!WdfRequestSend(request, Context->IoTarget, &options)) {
        status = WdfRequestGetStatus(request);
    }
    else {
        status = WdfRequestGetStatus(request);
    }

    WdfObjectDelete(request);
    return status;
}

static VOID
ApbL2capIndicationCallback(
    _In_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters)
{
    PAPB_DEVICE_CONTEXT ctx = (PAPB_DEVICE_CONTEXT)Context;
    UNREFERENCED_PARAMETER(Parameters);

    if (ctx == NULL) {
        return;
    }

    switch (Indication) {
    case IndicationRecvPacket:
        InterlockedIncrement(&ctx->PendingRecvCount);
        WdfWorkItemEnqueue(ctx->RecvWorkItem);
        break;

    case IndicationRemoteDisconnect:
        WdfWorkItemEnqueue(ctx->DisconnectWorkItem);
        break;

    default:
        break;
    }
}

VOID
ApbEvtRecvWorkItem(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(device);

    while (InterlockedDecrement(&ctx->PendingRecvCount) >= 0) {
        L2CAP_CHANNEL_HANDLE handle = NULL;
        BTH_ADDR remoteAddr = 0;

        WdfSpinLockAcquire(ctx->StateLock);
        if (ctx->ChannelOpen) {
            handle = ctx->ChannelHandle;
            remoteAddr = ctx->RemoteAddress;
        }
        WdfSpinLockRelease(ctx->StateLock);

        if (handle == NULL || !ctx->HasProfileInterface) {
            break;
        }

        PBRB brb = ctx->ProfileInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, APB_POOL_TAG);
        if (brb == NULL) {
            break;
        }

        PVOID rxBuf = ExAllocatePool2(POOL_FLAG_NON_PAGED, APB_AAP_MAX_PACKET_SIZE, APB_POOL_TAG);
        if (rxBuf == NULL) {
            ctx->ProfileInterface.BthFreeBrb(brb);
            break;
        }

        struct _BRB_L2CA_ACL_TRANSFER *xfer = &brb->BrbL2caAclTransfer;
        xfer->BtAddress = remoteAddr;
        xfer->ChannelHandle = handle;
        xfer->TransferFlags = ACL_TRANSFER_DIRECTION_IN | ACL_SHORT_TRANSFER_OK;
        xfer->BufferSize = APB_AAP_MAX_PACKET_SIZE;
        xfer->Buffer = rxBuf;
        xfer->BufferMDL = NULL;
        xfer->Timeout = 0;

        NTSTATUS status = ApbSubmitBrbSynchronously(ctx, brb, 3);
        if (NT_SUCCESS(status) && xfer->BufferSize > 0) {
            ApbCompleteOrQueueInboundPacket(ctx, rxBuf, xfer->BufferSize);
        }

        ExFreePoolWithTag(rxBuf, APB_POOL_TAG);
        ctx->ProfileInterface.BthFreeBrb(brb);
    }

    // Clamp underflow from the terminating decrement back to 0.
    InterlockedExchange(&ctx->PendingRecvCount, 0);
}

VOID
ApbEvtDisconnectWorkItem(_In_ WDFWORKITEM WorkItem)
{
    WDFDEVICE device = (WDFDEVICE)WdfWorkItemGetParentObject(WorkItem);
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(device);

    ApbL2capCloseChannel(ctx);
    ApbDrainPendingReads(ctx, STATUS_CONNECTION_DISCONNECTED);
}

static NTSTATUS
ApbSubmitOpenChannelBrb(
    _In_ PAPB_DEVICE_CONTEXT Context,
    _In_ BOOLEAN UseLowLatencyQos,
    _In_ ULONG TargetLatencyUs)
{
    PBRB brb = NULL;
    struct _BRB_L2CA_OPEN_CHANNEL *openBrb;
    NTSTATUS status;

    brb = Context->ProfileInterface.BthAllocateBrb(BRB_L2CA_OPEN_CHANNEL, APB_POOL_TAG);
    if (brb == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    openBrb = &brb->BrbL2caOpenChannel;
    openBrb->BtAddress = Context->RemoteAddress;
    openBrb->Psm = Context->L2capPsm;
    openBrb->ChannelFlags = CF_ROLE_EITHER;

    openBrb->ConfigOut.Flags = CFG_MTU;
    openBrb->ConfigOut.Mtu.Min = L2CAP_MIN_MTU;
    openBrb->ConfigOut.Mtu.Max = APB_AAP_MAX_PACKET_SIZE;
    openBrb->ConfigOut.Mtu.Preferred = APB_AAP_MAX_PACKET_SIZE;

    if (UseLowLatencyQos) {
        ULONG flushMs = (TargetLatencyUs + 999u) / 1000u;
        if (flushMs < L2CAP_MIN_FLUSHTO) {
            flushMs = L2CAP_MIN_FLUSHTO;
        }
        else if (flushMs > 0xFFFFu) {
            flushMs = 0xFFFFu;
        }
        // Request local HCI QoS guaranteed latency on the ACL link plus bounded flush
        // timeout (allowing negotiation up to default so AirPods never reject the channel).
        openBrb->ConfigOut.Flags |= CFG_FLUSHTO | CFG_QOS_LOCAL;
        openBrb->ConfigOut.FlushTO.Min = L2CAP_MIN_FLUSHTO;
        openBrb->ConfigOut.FlushTO.Max = L2CAP_DEFAULT_FLUSHTO;
        openBrb->ConfigOut.FlushTO.Preferred = (USHORT)flushMs;
        openBrb->ConfigOut.LocalQos.ServiceType = L2CAP_FLOW_SERVICE_TYPE_GUARANTEED;
        openBrb->ConfigOut.LocalQos.Latency = TargetLatencyUs;
    }

    openBrb->ConfigIn.Flags = CFG_MTU;
    openBrb->ConfigIn.Mtu.Min = L2CAP_MIN_MTU;
    openBrb->ConfigIn.Mtu.Max = APB_AAP_MAX_PACKET_SIZE;
    openBrb->ConfigIn.Mtu.Preferred = APB_AAP_MAX_PACKET_SIZE;

    openBrb->CallbackFlags = CALLBACK_DISCONNECT | CALLBACK_RECV_PACKET;
    openBrb->Callback = ApbL2capIndicationCallback;
    openBrb->CallbackContext = Context;
    openBrb->ReferenceObject = (PVOID)WdfDeviceWdmGetDeviceObject(Context->Device);
    openBrb->IncomingQueueDepth = 16;

    status = ApbSubmitBrbSynchronously(Context, brb, 8);
    if (NT_SUCCESS(status)) {
        WdfSpinLockAcquire(Context->StateLock);
        Context->ChannelHandle = openBrb->ChannelHandle;
        Context->NegotiatedMtu =
            openBrb->OutResults.Params.Mtu != 0 ? openBrb->OutResults.Params.Mtu
                                                : APB_AAP_MAX_PACKET_SIZE;
        Context->ChannelOpen = TRUE;
        WdfSpinLockRelease(Context->StateLock);
    }

    Context->ProfileInterface.BthFreeBrb(brb);
    return status;
}

NTSTATUS
ApbL2capOpenChannel(_In_ PAPB_DEVICE_CONTEXT Context)
{
    BOOLEAN lowLatency = FALSE;
    ULONG targetLatencyUs = APB_AAP_DEFAULT_LOW_LATENCY_US;
    NTSTATUS status;

    if (!Context->HasProfileInterface || Context->RemoteAddress == 0) {
        return STATUS_DEVICE_NOT_READY;
    }

    WdfSpinLockAcquire(Context->StateLock);
    if (Context->ChannelOpen) {
        WdfSpinLockRelease(Context->StateLock);
        return STATUS_SUCCESS;
    }
    lowLatency = Context->LowLatencyMode;
    targetLatencyUs = Context->TargetLatencyUs;
    WdfSpinLockRelease(Context->StateLock);

    status = ApbSubmitOpenChannelBrb(Context, lowLatency, targetLatencyUs);
    if (!NT_SUCCESS(status) && lowLatency) {
        // Some host Bluetooth controllers reject guaranteed HCI QoS setup; fall back to
        // standard L2CAP MTU configuration so two-way AAP control still connects reliably.
        status = ApbSubmitOpenChannelBrb(Context, FALSE, targetLatencyUs);
    }
    return status;
}

VOID
ApbL2capCloseChannel(_In_ PAPB_DEVICE_CONTEXT Context)
{
    L2CAP_CHANNEL_HANDLE handle = NULL;
    BTH_ADDR remoteAddr = 0;

    WdfSpinLockAcquire(Context->StateLock);
    if (Context->ChannelOpen) {
        handle = Context->ChannelHandle;
        remoteAddr = Context->RemoteAddress;
        Context->ChannelHandle = NULL;
        Context->ChannelOpen = FALSE;
    }
    WdfSpinLockRelease(Context->StateLock);

    if (handle == NULL || !Context->HasProfileInterface) {
        return;
    }

    PBRB brb = Context->ProfileInterface.BthAllocateBrb(BRB_L2CA_CLOSE_CHANNEL, APB_POOL_TAG);
    if (brb == NULL) {
        return;
    }

    brb->BrbL2caCloseChannel.BtAddress = remoteAddr;
    brb->BrbL2caCloseChannel.ChannelHandle = handle;
    (VOID)ApbSubmitBrbSynchronously(Context, brb, 3);
    Context->ProfileInterface.BthFreeBrb(brb);
}

NTSTATUS
ApbL2capSendPacket(
    _In_ PAPB_DEVICE_CONTEXT Context,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    L2CAP_CHANNEL_HANDLE handle = NULL;
    BTH_ADDR remoteAddr = 0;

    if (!Context->HasProfileInterface) {
        return STATUS_DEVICE_NOT_READY;
    }

    WdfSpinLockAcquire(Context->StateLock);
    if (Context->ChannelOpen) {
        handle = Context->ChannelHandle;
        remoteAddr = Context->RemoteAddress;
    }
    WdfSpinLockRelease(Context->StateLock);

    if (handle == NULL) {
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    PBRB brb = Context->ProfileInterface.BthAllocateBrb(BRB_L2CA_ACL_TRANSFER, APB_POOL_TAG);
    if (brb == NULL) {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PVOID txBuf = ExAllocatePool2(POOL_FLAG_NON_PAGED, Length, APB_POOL_TAG);
    if (txBuf == NULL) {
        Context->ProfileInterface.BthFreeBrb(brb);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(txBuf, Buffer, Length);

    struct _BRB_L2CA_ACL_TRANSFER *xfer = &brb->BrbL2caAclTransfer;
    xfer->BtAddress = remoteAddr;
    xfer->ChannelHandle = handle;
    xfer->TransferFlags = ACL_TRANSFER_DIRECTION_OUT;
    xfer->BufferSize = Length;
    xfer->Buffer = txBuf;
    xfer->BufferMDL = NULL;
    xfer->Timeout = 0;

    NTSTATUS status = ApbSubmitBrbSynchronously(Context, brb, 3);

    ExFreePoolWithTag(txBuf, APB_POOL_TAG);
    Context->ProfileInterface.BthFreeBrb(brb);
    return status;
}
