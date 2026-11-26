// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <initguid.h>

#include "Driver.h"

DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD ApbEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE ApbEvtDevicePrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE ApbEvtDeviceReleaseHardware;
EVT_WDF_FILE_CLEANUP ApbEvtFileCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL ApbEvtIoDeviceControl;
EVT_WDF_IO_QUEUE_IO_READ ApbEvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE ApbEvtIoWrite;

static NTSTATUS
ApbQueryBthEnumInfo(
    _In_ WDFIOTARGET Target,
    _In_ ULONG IoControlCode,
    _Out_writes_bytes_(OutputSize) PVOID OutputBuffer,
    _In_ ULONG OutputSize)
{
    WDF_MEMORY_DESCRIPTOR outputDesc;
    WDF_REQUEST_SEND_OPTIONS options;

    RtlZeroMemory(OutputBuffer, OutputSize);
    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&outputDesc, OutputBuffer, OutputSize);
    WDF_REQUEST_SEND_OPTIONS_INIT(&options, WDF_REQUEST_SEND_OPTION_SYNCHRONOUS);
    WDF_REQUEST_SEND_OPTIONS_SET_TIMEOUT(&options, WDF_REL_TIMEOUT_IN_SEC(5));

    return WdfIoTargetSendInternalIoctlSynchronously(
        Target, NULL, IoControlCode, NULL, &outputDesc, &options, NULL);
}

NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config, ApbEvtDeviceAdd);
    config.DriverPoolTag = APB_POOL_TAG;

    return WdfDriverCreate(DriverObject, RegistryPath, WDF_NO_OBJECT_ATTRIBUTES, &config, WDF_NO_HANDLE);
}

NTSTATUS
ApbEvtDeviceAdd(_In_ WDFDRIVER Driver, _Inout_ PWDFDEVICE_INIT DeviceInit)
{
    NTSTATUS status;
    WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDFDEVICE device;
    PAPB_DEVICE_CONTEXT ctx;
    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_WORKITEM_CONFIG workConfig;

    UNREFERENCED_PARAMETER(Driver);

    WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoBuffered);

    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);
    pnpCallbacks.EvtDevicePrepareHardware = ApbEvtDevicePrepareHardware;
    pnpCallbacks.EvtDeviceReleaseHardware = ApbEvtDeviceReleaseHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);

    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig, WDF_NO_EVENT_CALLBACK, WDF_NO_EVENT_CALLBACK, ApbEvtFileCleanup);
    WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, APB_DEVICE_CONTEXT);
    status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    ctx = ApbGetDeviceContext(device);
    RtlZeroMemory(ctx, sizeof(*ctx));
    ctx->Device = device;
    ctx->IoTarget = WdfDeviceGetIoTarget(device);
    ctx->L2capPsm = APB_AAP_DEFAULT_PSM;
    ctx->NegotiatedMtu = APB_AAP_MAX_PACKET_SIZE;
    ctx->LowLatencyMode = FALSE;
    ctx->TargetLatencyUs = APB_AAP_DEFAULT_LOW_LATENCY_US;

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = device;
    status = WdfSpinLockCreate(&attributes, &ctx->StateLock);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_WORKITEM_CONFIG_INIT(&workConfig, ApbEvtRecvWorkItem);
    workConfig.AutomaticSerialization = FALSE;
    status = WdfWorkItemCreate(&workConfig, &attributes, &ctx->RecvWorkItem);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_WORKITEM_CONFIG_INIT(&workConfig, ApbEvtDisconnectWorkItem);
    workConfig.AutomaticSerialization = FALSE;
    status = WdfWorkItemCreate(&workConfig, &attributes, &ctx->DisconnectWorkItem);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);
    queueConfig.EvtIoDeviceControl = ApbEvtIoDeviceControl;
    queueConfig.EvtIoRead = ApbEvtIoRead;
    queueConfig.EvtIoWrite = ApbEvtIoWrite;
    status = WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &ctx->DefaultQueue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);
    status =
        WdfIoQueueCreate(device, &queueConfig, WDF_NO_OBJECT_ATTRIBUTES, &ctx->PendingReadsQueue);
    if (!NT_SUCCESS(status)) {
        return status;
    }

    status = WdfDeviceCreateDeviceInterface(device, &GUID_DEVINTERFACE_APB_AAP, NULL);
    return status;
}

NTSTATUS
ApbEvtDevicePrepareHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesRaw,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    NTSTATUS status;
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(Device);
    BTH_DEVICE_INFO devInfo;
    BTH_ENUMERATOR_INFO enumInfo;

    UNREFERENCED_PARAMETER(ResourcesRaw);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    status = WdfFdoQueryForInterface(
        Device, &GUID_BTHDDI_PROFILE_DRIVER_INTERFACE,
        (PINTERFACE)&ctx->ProfileInterface, sizeof(ctx->ProfileInterface),
        BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI, NULL);
    if (!NT_SUCCESS(status)) {
        return status;
    }
    ctx->HasProfileInterface = TRUE;

    status = ApbQueryBthEnumInfo(
        ctx->IoTarget, IOCTL_INTERNAL_BTHENUM_GET_DEVINFO, &devInfo, sizeof(devInfo));
    if (NT_SUCCESS(status)) {
        ctx->RemoteAddress = devInfo.address;
    }

    status = ApbQueryBthEnumInfo(
        ctx->IoTarget, IOCTL_INTERNAL_BTHENUM_GET_ENUMINFO, &enumInfo, sizeof(enumInfo));
    if (NT_SUCCESS(status) && enumInfo.Port != 0 && enumInfo.Port <= 0xFFFFu) {
        ctx->L2capPsm = (USHORT)enumInfo.Port;
    }
    else {
        ctx->L2capPsm = APB_AAP_DEFAULT_PSM;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
ApbEvtDeviceReleaseHardware(
    _In_ WDFDEVICE Device,
    _In_ WDFCMRESLIST ResourcesTranslated)
{
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(Device);
    UNREFERENCED_PARAMETER(ResourcesTranslated);

    ApbL2capCloseChannel(ctx);
    WdfWorkItemFlush(ctx->RecvWorkItem);
    WdfWorkItemFlush(ctx->DisconnectWorkItem);
    ApbDrainPendingReads(ctx, STATUS_DEVICE_REMOVED);

    if (ctx->HasProfileInterface && ctx->ProfileInterface.Interface.InterfaceDereference != NULL) {
        ctx->ProfileInterface.Interface.InterfaceDereference(
            ctx->ProfileInterface.Interface.Context);
        ctx->HasProfileInterface = FALSE;
    }
    return STATUS_SUCCESS;
}

VOID
ApbEvtFileCleanup(_In_ WDFFILEOBJECT FileObject)
{
    WDFDEVICE device = WdfFileObjectGetDevice(FileObject);
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(device);

    ApbL2capCloseChannel(ctx);
    ApbDrainPendingReads(ctx, STATUS_CANCELLED);
}

VOID
ApbCompleteOrQueueInboundPacket(
    _In_ PAPB_DEVICE_CONTEXT Context,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length)
{
    WDFREQUEST pendingReq = NULL;
    NTSTATUS status;

    if (Length == 0 || Length > APB_AAP_MAX_PACKET_SIZE) {
        return;
    }

    status = WdfIoQueueRetrieveNextRequest(Context->PendingReadsQueue, &pendingReq);
    if (NT_SUCCESS(status) && pendingReq != NULL) {
        PVOID outBuf = NULL;
        size_t outLen = 0;
        status = WdfRequestRetrieveOutputBuffer(pendingReq, Length, &outBuf, &outLen);
        if (NT_SUCCESS(status) && outBuf != NULL) {
            RtlCopyMemory(outBuf, Buffer, Length);
            WdfRequestCompleteWithInformation(pendingReq, STATUS_SUCCESS, Length);
        }
        else {
            WdfRequestComplete(pendingReq, STATUS_BUFFER_TOO_SMALL);
        }
        return;
    }

    WdfSpinLockAcquire(Context->StateLock);
    if (Context->RxCount == APB_RX_RING_CAPACITY) {
        // Drop oldest frame under backpressure so fresh telemetry always wins.
        Context->RxHead = (Context->RxHead + 1) % APB_RX_RING_CAPACITY;
        Context->RxCount--;
    }
    Context->RxRing[Context->RxTail].Length = Length;
    RtlCopyMemory(Context->RxRing[Context->RxTail].Data, Buffer, Length);
    Context->RxTail = (Context->RxTail + 1) % APB_RX_RING_CAPACITY;
    Context->RxCount++;
    WdfSpinLockRelease(Context->StateLock);
}

VOID
ApbDrainPendingReads(_In_ PAPB_DEVICE_CONTEXT Context, _In_ NTSTATUS CompletionStatus)
{
    WDFREQUEST req = NULL;
    while (NT_SUCCESS(WdfIoQueueRetrieveNextRequest(Context->PendingReadsQueue, &req)) &&
           req != NULL)
    {
        WdfRequestComplete(req, CompletionStatus);
        req = NULL;
    }

    WdfSpinLockAcquire(Context->StateLock);
    Context->RxHead = 0;
    Context->RxTail = 0;
    Context->RxCount = 0;
    WdfSpinLockRelease(Context->StateLock);
}

VOID
ApbEvtIoDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(device);
    NTSTATUS status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR info = 0;

    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode) {
    case IOCTL_APB_AAP_GET_INFO: {
        PAPB_AAP_DEVICE_INFO outInfo = NULL;
        status = WdfRequestRetrieveOutputBuffer(Request, sizeof(*outInfo), (PVOID *)&outInfo, NULL);
        if (NT_SUCCESS(status)) {
            RtlZeroMemory(outInfo, sizeof(*outInfo));
            WdfSpinLockAcquire(ctx->StateLock);
            outInfo->BluetoothAddress = (UINT64)ctx->RemoteAddress;
            outInfo->L2capPsm = (UINT16)ctx->L2capPsm;
            outInfo->MaxMtu = (UINT16)ctx->NegotiatedMtu;
            outInfo->IsChannelOpen = ctx->ChannelOpen ? 1u : 0u;
            outInfo->LowLatencyMode = ctx->LowLatencyMode ? 1u : 0u;
            WdfSpinLockRelease(ctx->StateLock);
            info = sizeof(*outInfo);
        }
        break;
    }

    case IOCTL_APB_AAP_CONNECT:
        status = ApbL2capOpenChannel(ctx);
        break;

    case IOCTL_APB_AAP_DISCONNECT:
        ApbL2capCloseChannel(ctx);
        ApbDrainPendingReads(ctx, STATUS_CANCELLED);
        status = STATUS_SUCCESS;
        break;

    case IOCTL_APB_AAP_SET_LOW_LATENCY: {
        PAPB_AAP_LOW_LATENCY_CONFIG cfg = NULL;
        status = WdfRequestRetrieveInputBuffer(Request, sizeof(*cfg), (PVOID *)&cfg, NULL);
        if (NT_SUCCESS(status) && cfg != NULL) {
            ULONG targetUs =
                cfg->TargetLatencyUs != 0u ? cfg->TargetLatencyUs : APB_AAP_DEFAULT_LOW_LATENCY_US;
            if (targetUs < 5000u) {
                targetUs = 5000u;
            }
            else if (targetUs > 100000u) {
                targetUs = 100000u;
            }
            WdfSpinLockAcquire(ctx->StateLock);
            ctx->LowLatencyMode = cfg->Enable != 0u ? TRUE : FALSE;
            ctx->TargetLatencyUs = targetUs;
            WdfSpinLockRelease(ctx->StateLock);
            status = STATUS_SUCCESS;
        }
        break;
    }

    default:
        status = STATUS_INVALID_DEVICE_REQUEST;
        break;
    }

    WdfRequestCompleteWithInformation(Request, status, info);
}

VOID
ApbEvtIoRead(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t Length)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(device);
    PVOID outBuf = NULL;
    NTSTATUS status;
    ULONG copyLen = 0;
    BOOLEAN open = FALSE;

    if (Length == 0) {
        WdfRequestCompleteWithInformation(Request, STATUS_SUCCESS, 0);
        return;
    }

    status = WdfRequestRetrieveOutputBuffer(Request, Length, &outBuf, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    WdfSpinLockAcquire(ctx->StateLock);
    open = ctx->ChannelOpen;
    if (ctx->RxCount > 0) {
        PAPB_RX_SLOT slot = &ctx->RxRing[ctx->RxHead];
        if (slot->Length <= Length) {
            copyLen = slot->Length;
            RtlCopyMemory(outBuf, slot->Data, copyLen);
            ctx->RxHead = (ctx->RxHead + 1) % APB_RX_RING_CAPACITY;
            ctx->RxCount--;
            status = STATUS_SUCCESS;
        }
        else {
            status = STATUS_BUFFER_TOO_SMALL;
        }
        WdfSpinLockRelease(ctx->StateLock);
        WdfRequestCompleteWithInformation(Request, status, copyLen);
        return;
    }
    WdfSpinLockRelease(ctx->StateLock);

    if (!open) {
        WdfRequestComplete(Request, STATUS_DEVICE_NOT_CONNECTED);
        return;
    }

    status = WdfRequestForwardToIoQueue(Request, ctx->PendingReadsQueue);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
    }
}

VOID
ApbEvtIoWrite(_In_ WDFQUEUE Queue, _In_ WDFREQUEST Request, _In_ size_t Length)
{
    WDFDEVICE device = WdfIoQueueGetDevice(Queue);
    PAPB_DEVICE_CONTEXT ctx = ApbGetDeviceContext(device);
    PVOID inBuf = NULL;
    NTSTATUS status;

    if (Length == 0 || Length > APB_AAP_MAX_PACKET_SIZE) {
        WdfRequestComplete(Request, STATUS_INVALID_BUFFER_SIZE);
        return;
    }

    status = WdfRequestRetrieveInputBuffer(Request, Length, &inBuf, NULL);
    if (!NT_SUCCESS(status)) {
        WdfRequestComplete(Request, status);
        return;
    }

    status = ApbL2capSendPacket(ctx, inBuf, (ULONG)Length);
    WdfRequestCompleteWithInformation(Request, status, NT_SUCCESS(status) ? Length : 0);
}
