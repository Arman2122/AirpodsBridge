// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/AirPodsTransport_win.h"

#include <cfgmgr32.h>
#include <initguid.h>

#include <array>
#include <vector>

#include "Driver/BthAapBridge/Public.h"
#include "Source/Support/Logger.h"

namespace Core::AirPods {

namespace {

std::vector<std::wstring> EnumerateAapInterfaces()
{
    ULONG listChars = 0;
    GUID ifaceGuid = GUID_DEVINTERFACE_APB_AAP;
    CONFIGRET cr = CM_Get_Device_Interface_List_SizeW(
        &listChars, &ifaceGuid, nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS || listChars <= 1) {
        return {};
    }

    std::vector<wchar_t> buffer(listChars, L'\0');
    cr = CM_Get_Device_Interface_ListW(
        &ifaceGuid, nullptr, buffer.data(), listChars, CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
    if (cr != CR_SUCCESS) {
        return {};
    }

    std::vector<std::wstring> paths;
    const wchar_t *cursor = buffer.data();
    while (*cursor != L'\0') {
        paths.emplace_back(cursor);
        cursor += paths.back().size() + 1;
    }
    return paths;
}

} // namespace

L2capControlTransport::L2capControlTransport()
{
    CM_NOTIFY_FILTER filter{};
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_DEVINTERFACE_APB_AAP;

    HCMNOTIFICATION notifyHandle = nullptr;
    const CONFIGRET cr = CM_Register_Notification(
        &filter, this,
        [](HCMNOTIFICATION, PVOID context, CM_NOTIFY_ACTION action, PCM_NOTIFY_EVENT_DATA,
           DWORD) -> DWORD {
            auto *self = static_cast<L2capControlTransport *>(context);
            if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) {
                uint64_t addr = 0;
                {
                    std::lock_guard<std::mutex> lock{self->_mutex};
                    addr = self->_targetAddress;
                }
                if (addr != 0 && !self->IsConnected()) {
                    LOG(Info, "L2capControlTransport: AAP driver interface arrived; connecting.");
                    self->Connect(addr);
                }
            }
            else if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEREMOVAL) {
                LOG(Info, "L2capControlTransport: AAP driver interface removed.");
                self->Disconnect();
            }
            return ERROR_SUCCESS;
        },
        &notifyHandle);

    if (cr == CR_SUCCESS) {
        _cmNotification = notifyHandle;
    }
}

L2capControlTransport::~L2capControlTransport()
{
    if (_cmNotification != nullptr) {
        CM_Unregister_Notification(static_cast<HCMNOTIFICATION>(_cmNotification));
        _cmNotification = nullptr;
    }
    Disconnect();
}

bool L2capControlTransport::IsDriverInstalled()
{
    return !EnumerateAapInterfaces().empty();
}

bool L2capControlTransport::IsAvailable() const
{
    return IsDriverInstalled();
}

bool L2capControlTransport::IsConnected() const
{
    return _connected.load();
}

bool L2capControlTransport::QueryDeviceInfo(const std::wstring &path, uint64_t *addressOut)
{
    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    APB_AAP_DEVICE_INFO info{};
    DWORD bytesReturned = 0;
    const BOOL ok = DeviceIoControl(
        handle, IOCTL_APB_AAP_GET_INFO, nullptr, 0, &info, sizeof(info), &bytesReturned, nullptr);
    CloseHandle(handle);

    if (!ok || bytesReturned < sizeof(info)) {
        return false;
    }
    if (addressOut != nullptr) {
        *addressOut = info.BluetoothAddress;
    }
    return true;
}

std::wstring L2capControlTransport::FindInterfacePathForAddress(uint64_t bluetoothAddress)
{
    const auto paths = EnumerateAapInterfaces();
    if (paths.empty()) {
        return {};
    }

    for (const auto &path : paths) {
        uint64_t devAddress = 0;
        if (QueryDeviceInfo(path, &devAddress) && devAddress == bluetoothAddress) {
            return path;
        }
    }

    // If a single AAP PDO exists and address metadata wasn't reported, fall back to it.
    if (paths.size() == 1) {
        return paths.front();
    }
    return {};
}

void L2capControlTransport::Connect(uint64_t bluetoothAddress)
{
    if (bluetoothAddress == 0) {
        Disconnect();
        return;
    }

    std::thread oldThread;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _targetAddress = bluetoothAddress;
        CloseLocked(oldThread);
    }
    if (oldThread.joinable()) {
        oldThread.join();
    }

    const std::wstring path = FindInterfacePathForAddress(bluetoothAddress);
    if (path.empty()) {
        return;
    }

    HANDLE handle = CreateFileW(
        path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        LOG(Warn, "L2capControlTransport: CreateFileW failed (err={}).", GetLastError());
        return;
    }

    OVERLAPPED ioctlOv{};
    ioctlOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ioctlOv.hEvent == nullptr) {
        CloseHandle(handle);
        return;
    }

    // Configure L2CAP low-latency QoS / flush timeout on the kernel driver before opening
    // the channel so `BRB_L2CA_OPEN_CHANNEL` applies it immediately.
    {
        APB_AAP_LOW_LATENCY_CONFIG cfg{};
        cfg.Enable = _lowLatencyMode.load() ? 1u : 0u;
        cfg.TargetLatencyUs = APB_AAP_DEFAULT_LOW_LATENCY_US;
        DWORD cfgReturned = 0;
        ResetEvent(ioctlOv.hEvent);
        BOOL cfgOk = DeviceIoControl(
            handle, IOCTL_APB_AAP_SET_LOW_LATENCY, &cfg, sizeof(cfg), nullptr, 0, &cfgReturned,
            &ioctlOv);
        if (!cfgOk && GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(ioctlOv.hEvent, 1500);
        }
        ResetEvent(ioctlOv.hEvent);
    }

    DWORD returned = 0;
    BOOL ok =
        DeviceIoControl(handle, IOCTL_APB_AAP_CONNECT, nullptr, 0, nullptr, 0, &returned, &ioctlOv);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        const DWORD wait = WaitForSingleObject(ioctlOv.hEvent, 5000);
        if (wait == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(handle, &ioctlOv, &returned, FALSE);
        }
        else {
            CancelIoEx(handle, &ioctlOv);
            ok = FALSE;
        }
    }
    CloseHandle(ioctlOv.hEvent);

    if (!ok) {
        LOG(Warn, "L2capControlTransport: IOCTL_APB_AAP_CONNECT failed (err={}).", GetLastError());
        CloseHandle(handle);
        return;
    }

    HANDLE stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEvent == nullptr) {
        CloseHandle(handle);
        return;
    }

    {
        std::lock_guard<std::mutex> lock{_mutex};
        _deviceHandle = handle;
        _stopEvent = stopEvent;
        _connected.store(true);
        _readerThread = std::thread{&L2capControlTransport::ReaderLoop, this, handle, stopEvent};
    }

    LOG(Info, "L2capControlTransport: AAP L2CAP channel connected.");
    NotifyConnectionChanged(true);
}

void L2capControlTransport::Disconnect()
{
    const bool wasConnected = _connected.exchange(false);

    std::thread threadToJoin;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _targetAddress = 0;
        CloseLocked(threadToJoin);
    }

    if (threadToJoin.joinable() && threadToJoin.get_id() != std::this_thread::get_id()) {
        threadToJoin.join();
    }
    else if (threadToJoin.joinable()) {
        threadToJoin.detach();
    }

    if (wasConnected) {
        NotifyConnectionChanged(false);
    }
}

void L2capControlTransport::CloseLocked(std::thread &threadToJoin)
{
    _connected.store(false);
    if (_stopEvent != nullptr) {
        SetEvent(_stopEvent);
    }
    if (_deviceHandle != INVALID_HANDLE_VALUE) {
        CancelIoEx(_deviceHandle, nullptr);
        DWORD returned = 0;
        OVERLAPPED discOv{};
        discOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (discOv.hEvent != nullptr) {
            if (!DeviceIoControl(
                    _deviceHandle, IOCTL_APB_AAP_DISCONNECT, nullptr, 0, nullptr, 0, &returned,
                    &discOv) &&
                GetLastError() == ERROR_IO_PENDING)
            {
                WaitForSingleObject(discOv.hEvent, 1000);
            }
            CloseHandle(discOv.hEvent);
        }
        CloseHandle(_deviceHandle);
        _deviceHandle = INVALID_HANDLE_VALUE;
    }
    threadToJoin = std::move(_readerThread);
    if (_stopEvent != nullptr && !threadToJoin.joinable()) {
        CloseHandle(_stopEvent);
        _stopEvent = nullptr;
    }
}

bool L2capControlTransport::SendPacket(std::span<const uint8_t> packet)
{
    if (packet.empty() || packet.size() > APB_AAP_MAX_PACKET_SIZE || !_connected.load()) {
        return false;
    }

    HANDLE handle = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        handle = _deviceHandle;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
        return false;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(handle, packet.data(), static_cast<DWORD>(packet.size()), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ov.hEvent, 2000) == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(handle, &ov, &written, FALSE);
        }
        else {
            CancelIoEx(handle, &ov);
            ok = FALSE;
        }
    }
    CloseHandle(ov.hEvent);
    return ok != FALSE && written == packet.size();
}

void L2capControlTransport::SetLowLatencyMode(bool enabled)
{
    _lowLatencyMode.store(enabled);

    HANDLE handle = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        handle = _deviceHandle;
    }
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }

    APB_AAP_LOW_LATENCY_CONFIG cfg{};
    cfg.Enable = enabled ? 1u : 0u;
    cfg.TargetLatencyUs = APB_AAP_DEFAULT_LOW_LATENCY_US;

    OVERLAPPED ov{};
    ov.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ov.hEvent == nullptr) {
        return;
    }
    DWORD returned = 0;
    if (!DeviceIoControl(
            handle, IOCTL_APB_AAP_SET_LOW_LATENCY, &cfg, sizeof(cfg), nullptr, 0, &returned, &ov) &&
        GetLastError() == ERROR_IO_PENDING)
    {
        WaitForSingleObject(ov.hEvent, 1500);
    }
    CloseHandle(ov.hEvent);
}

void L2capControlTransport::SetOnPacketReceived(PacketCallback callback)
{
    std::lock_guard<std::mutex> lock{_callbackMutex};
    _onPacketReceived = std::move(callback);
}

void L2capControlTransport::SetOnConnectionChanged(ConnectionCallback callback)
{
    std::lock_guard<std::mutex> lock{_callbackMutex};
    _onConnectionChanged = std::move(callback);
}

void L2capControlTransport::ReaderLoop(HANDLE deviceHandle, HANDLE stopEvent)
{
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (readEvent == nullptr) {
        return;
    }

    std::array<uint8_t, APB_AAP_MAX_PACKET_SIZE> buffer{};

    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
        ResetEvent(readEvent);
        OVERLAPPED ov{};
        ov.hEvent = readEvent;

        DWORD bytesRead = 0;
        BOOL ok = ReadFile(
            deviceHandle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, &ov);
        if (!ok) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {stopEvent, readEvent};
                const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0) {
                    CancelIoEx(deviceHandle, &ov);
                    break;
                }
                if (wait != WAIT_OBJECT_0 + 1) {
                    break;
                }
                ok = GetOverlappedResult(deviceHandle, &ov, &bytesRead, FALSE);
            }
            if (!ok) {
                LOG(Info, "L2capControlTransport: read ended (err={}).", GetLastError());
                break;
            }
        }

        if (bytesRead > 0) {
            PacketCallback cb;
            {
                std::lock_guard<std::mutex> lock{_callbackMutex};
                cb = _onPacketReceived;
            }
            if (cb) {
                cb(std::vector<uint8_t>{buffer.begin(), buffer.begin() + bytesRead});
            }
        }
    }

    CloseHandle(readEvent);
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_stopEvent == stopEvent) {
            _stopEvent = nullptr;
        }
    }
    CloseHandle(stopEvent);

    if (_connected.exchange(false)) {
        NotifyConnectionChanged(false);
    }
}

void L2capControlTransport::NotifyConnectionChanged(bool connected)
{
    ConnectionCallback cb;
    {
        std::lock_guard<std::mutex> lock{_callbackMutex};
        cb = _onConnectionChanged;
    }
    if (cb) {
        cb(connected);
    }
}

//////////////////////////////////////////////////
// CompositeControlTransport
//

CompositeControlTransport::CompositeControlTransport(
    std::unique_ptr<ControlTransport> primary, std::unique_ptr<ControlTransport> fallback)
    : _primary{std::move(primary)}, _fallback{std::move(fallback)}
{
    auto bindChild = [this](ControlTransport *child) {
        if (child == nullptr) {
            return;
        }
        child->SetOnPacketReceived([this, child](std::vector<uint8_t> pkt) {
            PacketCallback cb;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                if (_active != child) {
                    return;
                }
                cb = _onPacket;
            }
            if (cb) {
                cb(std::move(pkt));
            }
        });
        child->SetOnConnectionChanged([this, child](bool connected) {
            ConnectionCallback cb;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                if (_active != child) {
                    return;
                }
                cb = _onConn;
            }
            if (cb) {
                cb(connected);
            }
        });
    };

    bindChild(_primary.get());
    bindChild(_fallback.get());
}

CompositeControlTransport::~CompositeControlTransport()
{
    Disconnect();
    if (_primary) {
        _primary->SetOnPacketReceived({});
        _primary->SetOnConnectionChanged({});
    }
    if (_fallback) {
        _fallback->SetOnPacketReceived({});
        _fallback->SetOnConnectionChanged({});
    }
}

ControlTransport *CompositeControlTransport::ActiveBackend() const
{
    if (_active != nullptr) {
        return _active;
    }
    if (_primary && _primary->IsAvailable()) {
        return _primary.get();
    }
    if (_fallback && _fallback->IsAvailable()) {
        return _fallback.get();
    }
    return nullptr;
}

bool CompositeControlTransport::IsAvailable() const
{
    return (_primary && _primary->IsAvailable()) || (_fallback && _fallback->IsAvailable());
}

bool CompositeControlTransport::IsConnected() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _active != nullptr && _active->IsConnected();
}

std::string CompositeControlTransport::BackendName() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (auto *backend = ActiveBackend(); backend != nullptr) {
        return backend->BackendName();
    }
    return "none";
}

void CompositeControlTransport::Connect(uint64_t bluetoothAddress)
{
    ControlTransport *chosen = nullptr;
    ControlTransport *other = nullptr;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_primary && _primary->IsAvailable()) {
            chosen = _primary.get();
            other = _fallback.get();
        }
        else if (_fallback && _fallback->IsAvailable()) {
            chosen = _fallback.get();
            other = _primary.get();
        }
        _active = chosen;
    }

    if (other != nullptr) {
        other->Disconnect();
    }
    if (chosen != nullptr) {
        LOG(Info, "CompositeControlTransport: connecting via backend '{}'.", chosen->BackendName());
        chosen->Connect(bluetoothAddress);
    }
}

void CompositeControlTransport::Disconnect()
{
    if (_primary) {
        _primary->Disconnect();
    }
    if (_fallback) {
        _fallback->Disconnect();
    }
    std::lock_guard<std::mutex> lock{_mutex};
    _active = nullptr;
}

bool CompositeControlTransport::SendPacket(std::span<const uint8_t> packet)
{
    ControlTransport *active = nullptr;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        active = _active;
    }
    return active != nullptr && active->SendPacket(packet);
}

void CompositeControlTransport::SetLowLatencyMode(bool enabled)
{
    if (_primary) {
        _primary->SetLowLatencyMode(enabled);
    }
    if (_fallback) {
        _fallback->SetLowLatencyMode(enabled);
    }
}

std::optional<RadioServiceInfo> CompositeControlTransport::GetRadioServiceInfo() const
{
    if (_primary) {
        if (auto info = _primary->GetRadioServiceInfo(); info.has_value()) {
            return info;
        }
    }
    return _fallback ? _fallback->GetRadioServiceInfo() : std::nullopt;
}

bool CompositeControlTransport::ApplyRadioProxyOptions(const RadioProxyOptions &options)
{
    return _primary && _primary->ApplyRadioProxyOptions(options);
}

bool CompositeControlTransport::ConfigureVirtualMic(
    uint64_t bluetoothAddress, bool enable, bool hdVoice, const std::string &deviceName)
{
    return _primary && _primary->ConfigureVirtualMic(bluetoothAddress, enable, hdVoice, deviceName);
}

bool CompositeControlTransport::RetryRadioServiceAfterGuard()
{
    return _primary && _primary->RetryRadioServiceAfterGuard();
}

bool CompositeControlTransport::RestoreNativeRadioDriver()
{
    return _primary && _primary->RestoreNativeRadioDriver();
}

void CompositeControlTransport::SetOnPacketReceived(PacketCallback callback)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _onPacket = std::move(callback);
}

void CompositeControlTransport::SetOnConnectionChanged(ConnectionCallback callback)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _onConn = std::move(callback);
}

} // namespace Core::AirPods
