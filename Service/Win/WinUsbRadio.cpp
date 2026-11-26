// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/WinUsbRadio.h"
#include "Service/Win/ServiceLog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Service::Win {

namespace {

// Reusable manual-reset OVERLAPPED event per worker thread: eliminates ~200/s kernel event
// CreateEventW/CloseHandle cycles on the real-time A2DP Bulk OUT/IN audio path.
HANDLE ThreadOverlappedEvent()
{
    thread_local UniqueHandle ev{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (ev.Valid()) {
        ResetEvent(ev.Get());
    }
    return ev.Get();
}

uint32_t WaitOverlapped(
    WINUSB_INTERFACE_HANDLE iface, HANDLE fileHandle, OVERLAPPED &ov, DWORD &transferred,
    HANDLE cancelEvent)
{
    const DWORD err = GetLastError();
    if (err != ERROR_IO_PENDING) {
        return err;
    }

    if (cancelEvent != nullptr) {
        HANDLE waits[2] = {cancelEvent, ov.hEvent};
        const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (wait == WAIT_OBJECT_0) {
            CancelIoEx(fileHandle, &ov);
            WinUsb_GetOverlappedResult(iface, &ov, &transferred, TRUE);
            return ERROR_OPERATION_ABORTED;
        }
    }

    if (!WinUsb_GetOverlappedResult(iface, &ov, &transferred, TRUE)) {
        return GetLastError();
    }
    return ERROR_SUCCESS;
}

} // namespace

WinUsbRadio::~WinUsbRadio()
{
    Close();
}

bool WinUsbRadio::Open(const std::wstring &devicePath)
{
    Close();

    UniqueHandle file{CreateFileW(
        devicePath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED, nullptr)};
    if (!file.Valid()) {
        ServiceLog::Write(
            LogLevel::Error, "winusb",
            "CreateFileW failed on " + WideToUtf8(devicePath) + ": " +
                FormatWin32Error(GetLastError()));
        return false;
    }

    WINUSB_INTERFACE_HANDLE rawPrimary = nullptr;
    if (!WinUsb_Initialize(file.Get(), &rawPrimary)) {
        ServiceLog::Write(
            LogLevel::Error, "winusb",
            "WinUsb_Initialize failed: " + FormatWin32Error(GetLastError()));
        return false;
    }
    UniqueWinUsb primary{rawPrimary};

    // Read Device Descriptor (18 bytes).
    std::vector<uint8_t> devDesc(18, 0);
    ULONG got = 0;
    if (!WinUsb_GetDescriptor(
            primary.Get(), USB_DEVICE_DESCRIPTOR_TYPE, 0, 0, devDesc.data(),
            static_cast<ULONG>(devDesc.size()), &got) ||
        got < 18)
    {
        ServiceLog::Write(
            LogLevel::Error, "winusb",
            "Failed to read USB Device Descriptor: " + FormatWin32Error(GetLastError()));
        return false;
    }

    // Read Configuration Descriptor header first to learn exact wTotalLength.
    std::array<uint8_t, 9> cfgHead{};
    if (!WinUsb_GetDescriptor(
            primary.Get(), USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, cfgHead.data(),
            static_cast<ULONG>(cfgHead.size()), &got) ||
        got < 9)
    {
        ServiceLog::Write(
            LogLevel::Error, "winusb",
            "Failed to read USB Config Descriptor header: " + FormatWin32Error(GetLastError()));
        return false;
    }
    const uint16_t cfgTotalLen = static_cast<uint16_t>(cfgHead[2]) |
                                 static_cast<uint16_t>(static_cast<uint16_t>(cfgHead[3]) << 8);
    if (cfgTotalLen < 9) {
        return false;
    }

    std::vector<uint8_t> cfgDesc(cfgTotalLen, 0);
    if (!WinUsb_GetDescriptor(
            primary.Get(), USB_CONFIGURATION_DESCRIPTOR_TYPE, 0, 0, cfgDesc.data(),
            static_cast<ULONG>(cfgDesc.size()), &got) ||
        got < cfgTotalLen)
    {
        ServiceLog::Write(
            LogLevel::Error, "winusb",
            "Failed to read full USB Config Descriptor: " + FormatWin32Error(GetLastError()));
        return false;
    }

    // Query device speed.
    // Note: WinUsb_QueryDeviceInformation(DEVICE_SPEED) returns 0x01 (`LowSpeed`) for BOTH
    // Low-Speed and Full-Speed USB devices. Bluetooth HCI adapters use Bulk + Isochronous
    // endpoints (which USB 2.0 forbids on 1.5 Mbps Low-Speed), so 0x01 / 0x02 is Full-Speed
    // (12 Mbps = USB/IP speed 2). Reporting speed 1 (`Low Speed`) breaks usbip2_ude Bulk pipes.
    uint8_t speedByte = FullSpeed;
    ULONG speedLen = sizeof(speedByte);
    uint32_t usbIpSpeed = Protocol::UsbIp::kSpeedFull;
    if (WinUsb_QueryDeviceInformation(primary.Get(), DEVICE_SPEED, &speedLen, &speedByte)) {
        switch (speedByte) {
        case LowSpeed:
        case FullSpeed:
            usbIpSpeed = Protocol::UsbIp::kSpeedFull;
            break;
        case HighSpeed:
            usbIpSpeed = Protocol::UsbIp::kSpeedHigh;
            break;
        default:
            usbIpSpeed = Protocol::UsbIp::kSpeedSuper;
            break;
        }
    }

    std::lock_guard<std::mutex> lock{_ifaceMutex};
    _file = std::move(file);
    _primary = std::move(primary);
    _associated.clear();
    _deviceDesc = std::move(devDesc);
    _configDesc = std::move(cfgDesc);
    _usbIpSpeed = usbIpSpeed;
    _hciPipes = Protocol::UsbIp::FindBluetoothHciPipes(_configDesc);

    // Open all associated interfaces (0..7) so Interface 1 (SCO) and vendor interfaces work.
    for (UCHAR assocIdx = 0; assocIdx < 8; ++assocIdx) {
        WINUSB_INTERFACE_HANDLE assocHandle = nullptr;
        if (!WinUsb_GetAssociatedInterface(_primary.Get(), assocIdx, &assocHandle)) {
            break;
        }
        USB_INTERFACE_DESCRIPTOR desc{};
        if (WinUsb_QueryInterfaceSettings(assocHandle, 0, &desc)) {
            _associated.emplace(desc.bInterfaceNumber, UniqueWinUsb{assocHandle});
        }
        else {
            WinUsb_Free(assocHandle);
        }
    }

    RefreshPipeMapLocked();
    ConfigureHciPoliciesLocked();
    _open.store(true);

    char pipeBuf[128]{};
    std::snprintf(
        pipeBuf, sizeof(pipeBuf),
        "Opened WinUSB radio: speed=%u eventPipe=0x%02X aclIn=0x%02X aclOut=0x%02X", _usbIpSpeed,
        _hciPipes.eventPipe, _hciPipes.aclInPipe, _hciPipes.aclOutPipe);
    ServiceLog::Write(LogLevel::Info, "winusb", pipeBuf);
    return true;
}

void WinUsbRadio::Close()
{
    _open.store(false);
    AbortAllPipes();
    std::lock_guard<std::mutex> lock{_ifaceMutex};
    _pipeToInterface.clear();
    _associated.clear();
    _primary.Reset();
    _file.Reset();
}

bool WinUsbRadio::IsOpen() const noexcept
{
    return _open.load();
}

Protocol::UsbIp::ExportedDevice WinUsbRadio::MakeExportedDevice(std::string busId) const
{
    return Protocol::UsbIp::ExportedDevice{
        .busId = std::move(busId),
        .busNum = 1,
        .devNum = 1,
        .speed = _usbIpSpeed,
        .deviceDescriptor = _deviceDesc,
        .configDescriptor = _configDesc,
    };
}

uint32_t WinUsbRadio::ControlTransfer(
    const std::array<uint8_t, 8> &setup, std::span<const uint8_t> outData,
    std::vector<uint8_t> &inData, uint32_t &transferred, HANDLE cancelEvent)
{
    transferred = 0;
    WINUSB_INTERFACE_HANDLE primary = nullptr;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock{_ifaceMutex};
        if (!_primary.Valid() || !_file.Valid()) {
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        primary = _primary.Get();
        fileHandle = _file.Get();
    }

    WINUSB_SETUP_PACKET pkt{};
    pkt.RequestType = setup[0];
    pkt.Request = setup[1];
    pkt.Value =
        static_cast<USHORT>(setup[2]) | static_cast<USHORT>(static_cast<USHORT>(setup[3]) << 8);
    pkt.Index =
        static_cast<USHORT>(setup[4]) | static_cast<USHORT>(static_cast<USHORT>(setup[5]) << 8);
    pkt.Length =
        static_cast<USHORT>(setup[6]) | static_cast<USHORT>(static_cast<USHORT>(setup[7]) << 8);

    const bool isIn = (pkt.RequestType & 0x80u) != 0;
    std::vector<uint8_t> buf;
    if (isIn) {
        buf.resize(pkt.Length);
    }
    else {
        buf.assign(outData.begin(), outData.end());
    }

    HANDLE ovEvent = ThreadOverlappedEvent();
    if (ovEvent == nullptr) {
        return GetLastError();
    }
    OVERLAPPED ov{};
    ov.hEvent = ovEvent;

    ULONG xfer = 0;
    const BOOL ok = WinUsb_ControlTransfer(
        primary, pkt, buf.empty() ? nullptr : buf.data(), static_cast<ULONG>(buf.size()), &xfer,
        &ov);
    DWORD err = ERROR_SUCCESS;
    if (!ok) {
        DWORD ovXfer = 0;
        err = WaitOverlapped(primary, fileHandle, ov, ovXfer, cancelEvent);
        xfer = ovXfer;
    }

    if (err == ERROR_SUCCESS) {
        transferred = xfer;
        if (isIn) {
            buf.resize(std::min<size_t>(buf.size(), xfer));
            inData = std::move(buf);
        }
    }
    return err;
}

uint32_t WinUsbRadio::SetInterfaceAltSetting(uint8_t interfaceNumber, uint8_t altSetting)
{
    std::lock_guard<std::mutex> lock{_ifaceMutex};
    if (!_primary.Valid()) {
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    WINUSB_INTERFACE_HANDLE target = nullptr;
    USB_INTERFACE_DESCRIPTOR primaryDesc{};
    if (WinUsb_QueryInterfaceSettings(_primary.Get(), 0, &primaryDesc) &&
        primaryDesc.bInterfaceNumber == interfaceNumber)
    {
        target = _primary.Get();
    }
    else if (auto it = _associated.find(interfaceNumber); it != _associated.end()) {
        target = it->second.Get();
    }

    if (target == nullptr) {
        return ERROR_INVALID_PARAMETER;
    }

    if (!WinUsb_SetCurrentAlternateSetting(target, altSetting)) {
        return GetLastError();
    }

    RefreshPipeMapLocked();
    if (interfaceNumber == _hciPipes.interfaceNumber) {
        ConfigureHciPoliciesLocked();
    }
    return ERROR_SUCCESS;
}

uint32_t WinUsbRadio::ReadPipe(
    uint8_t pipeId, uint32_t maxLength, std::vector<uint8_t> &outBuffer, HANDLE cancelEvent)
{
    WINUSB_INTERFACE_HANDLE iface = nullptr;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock{_ifaceMutex};
        if (!_file.Valid()) {
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        iface = InterfaceForPipeLocked(pipeId);
        fileHandle = _file.Get();
    }
    if (iface == nullptr) {
        return ERROR_INVALID_HANDLE;
    }

    outBuffer.resize(maxLength);
    HANDLE ovEvent = ThreadOverlappedEvent();
    if (ovEvent == nullptr) {
        return GetLastError();
    }
    OVERLAPPED ov{};
    ov.hEvent = ovEvent;

    ULONG got = 0;
    const BOOL ok = WinUsb_ReadPipe(
        iface, pipeId, outBuffer.empty() ? nullptr : outBuffer.data(), maxLength, &got, &ov);
    DWORD err = ERROR_SUCCESS;
    if (!ok) {
        DWORD ovGot = 0;
        err = WaitOverlapped(iface, fileHandle, ov, ovGot, cancelEvent);
        got = ovGot;
    }
    if (err == ERROR_SUCCESS) {
        outBuffer.resize(std::min<size_t>(outBuffer.size(), got));
    }
    else {
        outBuffer.clear();
    }
    return err;
}

uint32_t WinUsbRadio::WritePipe(
    uint8_t pipeId, std::span<const uint8_t> data, uint32_t &written, HANDLE cancelEvent)
{
    written = 0;
    WINUSB_INTERFACE_HANDLE iface = nullptr;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock{_ifaceMutex};
        if (!_file.Valid()) {
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        iface = InterfaceForPipeLocked(pipeId);
        fileHandle = _file.Get();
    }
    if (iface == nullptr) {
        return ERROR_INVALID_HANDLE;
    }

    HANDLE ovEvent = ThreadOverlappedEvent();
    if (ovEvent == nullptr) {
        return GetLastError();
    }
    OVERLAPPED ov{};
    ov.hEvent = ovEvent;

    ULONG sent = 0;
    const BOOL ok = WinUsb_WritePipe(
        iface, pipeId, data.empty() ? nullptr : const_cast<PUCHAR>(data.data()),
        static_cast<ULONG>(data.size()), &sent, &ov);
    DWORD err = ERROR_SUCCESS;
    if (!ok) {
        DWORD ovSent = 0;
        err = WaitOverlapped(iface, fileHandle, ov, ovSent, cancelEvent);
        sent = ovSent;
    }
    if (err == ERROR_SUCCESS) {
        written = sent;
    }
    return err;
}

uint32_t WinUsbRadio::TransferIsoch(
    uint8_t pipeId, bool isIn, std::span<const uint8_t> outData, uint32_t packetCount,
    uint32_t bytesPerPacket, Protocol::UsbIp::IsoTransferResult &result, HANDLE cancelEvent)
{
    result = {};
    if (packetCount == 0 || bytesPerPacket == 0) {
        return ERROR_INVALID_PARAMETER;
    }

    WINUSB_INTERFACE_HANDLE iface = nullptr;
    HANDLE fileHandle = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock{_ifaceMutex};
        if (!_file.Valid()) {
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        iface = InterfaceForPipeLocked(pipeId);
        fileHandle = _file.Get();
    }
    if (iface == nullptr) {
        return ERROR_INVALID_HANDLE;
    }

    const uint32_t totalBytes = packetCount * bytesPerPacket;
    std::vector<uint8_t> buffer(totalBytes, 0);
    if (!isIn && !outData.empty()) {
        std::memcpy(buffer.data(), outData.data(), std::min<size_t>(buffer.size(), outData.size()));
    }

    WINUSB_ISOCH_BUFFER_HANDLE reg = nullptr;
    if (!WinUsb_RegisterIsochBuffer(iface, pipeId, buffer.data(), totalBytes, &reg)) {
        return GetLastError();
    }

    std::vector<USBD_ISO_PACKET_DESCRIPTOR> descs(packetCount);
    HANDLE ovEvent = ThreadOverlappedEvent();
    if (ovEvent == nullptr) {
        return GetLastError();
    }
    OVERLAPPED ov{};
    ov.hEvent = ovEvent;

    BOOL ok = FALSE;
    if (isIn) {
        ok = WinUsb_ReadIsochPipeAsap(reg, 0, totalBytes, FALSE, packetCount, descs.data(), &ov);
    }
    else {
        ok = WinUsb_WriteIsochPipeAsap(reg, 0, totalBytes, FALSE, &ov);
    }

    DWORD err = ERROR_SUCCESS;
    if (!ok) {
        DWORD xfer = 0;
        err = WaitOverlapped(iface, fileHandle, ov, xfer, cancelEvent);
    }

    WinUsb_UnregisterIsochBuffer(reg);

    if (err != ERROR_SUCCESS) {
        return err;
    }

    result.data = std::move(buffer);
    result.packets.resize(packetCount);
    for (uint32_t i = 0; i < packetCount; ++i) {
        if (isIn) {
            result.packets[i] = Protocol::UsbIp::IsoPacketResult{
                .offset = descs[i].Offset,
                .actualLength = descs[i].Length,
                .status =
                    descs[i].Status == 0 ? Protocol::UsbIp::kErrOk : Protocol::UsbIp::kErrExdev,
            };
        }
        else {
            result.packets[i] = Protocol::UsbIp::IsoPacketResult{
                .offset = i * bytesPerPacket,
                .actualLength = bytesPerPacket,
                .status = Protocol::UsbIp::kErrOk,
            };
        }
    }
    return ERROR_SUCCESS;
}

uint32_t WinUsbRadio::ResetPipe(uint8_t pipeId)
{
    std::lock_guard<std::mutex> lock{_ifaceMutex};
    auto iface = InterfaceForPipeLocked(pipeId);
    if (iface == nullptr) {
        return ERROR_INVALID_HANDLE;
    }
    return WinUsb_ResetPipe(iface, pipeId) ? ERROR_SUCCESS : GetLastError();
}

void WinUsbRadio::AbortAllPipes()
{
    std::lock_guard<std::mutex> lock{_ifaceMutex};
    for (const auto &[pipeId, iface] : _pipeToInterface) {
        WinUsb_AbortPipe(iface, pipeId);
    }
    if (_file.Valid()) {
        CancelIoEx(_file.Get(), nullptr);
    }
}

bool WinUsbRadio::IsFatalDeviceLossError(uint32_t win32Error) noexcept
{
    return win32Error == ERROR_DEVICE_NOT_CONNECTED || win32Error == ERROR_NO_SUCH_DEVICE ||
           win32Error == ERROR_INVALID_HANDLE || win32Error == ERROR_BAD_COMMAND;
}

WINUSB_INTERFACE_HANDLE WinUsbRadio::InterfaceForPipeLocked(uint8_t pipeId)
{
    if (const auto it = _pipeToInterface.find(pipeId); it != _pipeToInterface.end()) {
        return it->second;
    }
    RefreshPipeMapLocked();
    if (const auto it = _pipeToInterface.find(pipeId); it != _pipeToInterface.end()) {
        return it->second;
    }
    return _primary.Get();
}

void WinUsbRadio::RefreshPipeMapLocked()
{
    _pipeToInterface.clear();
    auto scan = [this](WINUSB_INTERFACE_HANDLE iface) {
        if (iface == nullptr) {
            return;
        }
        UCHAR alt = 0;
        if (!WinUsb_GetCurrentAlternateSetting(iface, &alt)) {
            alt = 0;
        }
        USB_INTERFACE_DESCRIPTOR desc{};
        if (!WinUsb_QueryInterfaceSettings(iface, alt, &desc)) {
            return;
        }
        for (UCHAR i = 0; i < desc.bNumEndpoints; ++i) {
            WINUSB_PIPE_INFORMATION pipe{};
            if (WinUsb_QueryPipe(iface, alt, i, &pipe)) {
                _pipeToInterface[pipe.PipeId] = iface;
            }
        }
    };

    scan(_primary.Get());
    for (const auto &[ifaceNum, handle] : _associated) {
        scan(handle.Get());
    }
}

void WinUsbRadio::ConfigureHciPoliciesLocked()
{
    if (!_primary.Valid()) {
        return;
    }

    // Prevent WinUSB from selectively suspending the physical Bluetooth radio into USB D2
    // while idle, which causes Intel/Realtek HCI Bulk OUT pipes to stall on wake.
    UCHAR autoSuspend = FALSE;
    const BOOL powerOk =
        WinUsb_SetPowerPolicy(_primary.Get(), AUTO_SUSPEND, sizeof(autoSuspend), &autoSuspend);

    // Bound Control EP0 transfers so a wedged HCI command can never block the EP0 worker forever.
    ULONG ep0TimeoutMs = 5000;
    const BOOL ep0TimeoutOk = WinUsb_SetPipePolicy(
        _primary.Get(), 0x00, PIPE_TRANSFER_TIMEOUT, sizeof(ep0TimeoutMs), &ep0TimeoutMs);

    BOOL zlpOk = FALSE;
    BOOL aclOutTimeoutOk = FALSE;
    BOOL aclOutAutoClearOk = FALSE;
    if (_hciPipes.aclOutPipe != 0) {
        if (WINUSB_INTERFACE_HANDLE outIface = InterfaceForPipeLocked(_hciPipes.aclOutPipe);
            outIface != nullptr)
        {
            // Bluetooth Core Spec Vol 4 Part B §2.1: any HCI ACL Bulk OUT transfer whose length is
            // an exact multiple of wMaxPacketSize (e.g. 64 B on Full-Speed USB) MUST be terminated
            // with a Zero-Length Packet (ZLP). Without SHORT_PACKET_TERMINATE, fixed-size CBR SBC
            // A2DP frames or 64-byte-multiple ACL packets corrupt USB framing and wedge EP 0x02.
            UCHAR enable = TRUE;
            zlpOk = WinUsb_SetPipePolicy(
                outIface, _hciPipes.aclOutPipe, SHORT_PACKET_TERMINATE, sizeof(enable), &enable);

            // Bound Bulk OUT writes so a dead endpoint cannot hang forever while leaving plenty of
            // headroom (4 s) for transient Bluetooth baseband retransmissions without false alarms.
            ULONG outTimeoutMs = 4000;
            aclOutTimeoutOk = WinUsb_SetPipePolicy(
                outIface, _hciPipes.aclOutPipe, PIPE_TRANSFER_TIMEOUT, sizeof(outTimeoutMs),
                &outTimeoutMs);

            // Best-effort auto-clear stall on OUT pipe (supported on newer WinUSB builds).
            aclOutAutoClearOk = WinUsb_SetPipePolicy(
                outIface, _hciPipes.aclOutPipe, AUTO_CLEAR_STALL, sizeof(enable), &enable);
#ifdef RESET_PIPE_ON_RESUME
            WinUsb_SetPipePolicy(
                outIface, _hciPipes.aclOutPipe, RESET_PIPE_ON_RESUME, sizeof(enable), &enable);
#endif
        }
    }

    auto configureInPipe = [this](uint8_t pipeId) -> BOOL {
        if (pipeId == 0) {
            return FALSE;
        }
        WINUSB_INTERFACE_HANDLE inIface = InterfaceForPipeLocked(pipeId);
        if (inIface == nullptr) {
            return FALSE;
        }
        // Keep PIPE_TRANSFER_TIMEOUT = 0 (infinite) on Interrupt IN / Bulk IN because Windows'
        // BTHPORT posts persistent reads waiting for asynchronous Bluetooth events and ACL data.
        UCHAR enable = TRUE;
        const BOOL clearOk =
            WinUsb_SetPipePolicy(inIface, pipeId, AUTO_CLEAR_STALL, sizeof(enable), &enable);
#ifdef RESET_PIPE_ON_RESUME
        WinUsb_SetPipePolicy(inIface, pipeId, RESET_PIPE_ON_RESUME, sizeof(enable), &enable);
#endif
        return clearOk;
    };

    const BOOL eventAutoClearOk = configureInPipe(_hciPipes.eventPipe);
    const BOOL aclInAutoClearOk = configureInPipe(_hciPipes.aclInPipe);

    char msg[256]{};
    std::snprintf(
        msg, sizeof(msg),
        "Configured WinUSB HCI policies: autoSuspendOff=%d ep0Timeout=%d zlpOut=%d "
        "aclOutTimeout=%d autoClearStall(ev=%d,in=%d,out=%d)",
        powerOk ? 1 : 0, ep0TimeoutOk ? 1 : 0, zlpOk ? 1 : 0, aclOutTimeoutOk ? 1 : 0,
        eventAutoClearOk ? 1 : 0, aclInAutoClearOk ? 1 : 0, aclOutAutoClearOk ? 1 : 0);
    ServiceLog::Write(LogLevel::Info, "winusb", msg);
}

} // namespace Service::Win
