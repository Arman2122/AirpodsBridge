// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "Source/Core/AirPodsTransport.h"
#include "Source/Support/OS/Windows.h"

namespace Core::AirPods {

//
// User-mode client for the `BthAapBridge` KMDF Bluetooth L2CAP profile driver.
//
// Uses `CM_Register_Notification` on `GUID_DEVINTERFACE_APB_AAP` so idle cost is strictly
// zero when the driver is not installed or no AirPods device is attached. When the driver
// interface appears and `Connect(address)` is called, opens an overlapped handle, issues
// `IOCTL_APB_AAP_CONNECT`, and streams raw AAP frames bidirectionally.
//
class L2capControlTransport final : public ControlTransport
{
public:
    L2capControlTransport();
    ~L2capControlTransport() override;

    L2capControlTransport(const L2capControlTransport &) = delete;
    L2capControlTransport &operator=(const L2capControlTransport &) = delete;

    static bool IsDriverInstalled();

    bool IsAvailable() const override;
    bool IsConnected() const override;
    std::string BackendName() const override
    {
        return "kmdf_l2cap";
    }

    void Connect(uint64_t bluetoothAddress) override;
    void Disconnect() override;
    bool SendPacket(std::span<const uint8_t> packet) override;
    void SetLowLatencyMode(bool enabled) override;

    void SetOnPacketReceived(PacketCallback callback) override;
    void SetOnConnectionChanged(ConnectionCallback callback) override;

private:
    static std::wstring FindInterfacePathForAddress(uint64_t bluetoothAddress);
    static bool QueryDeviceInfo(const std::wstring &path, uint64_t *addressOut);

    void ReaderLoop(HANDLE deviceHandle, HANDLE stopEvent);
    void NotifyConnectionChanged(bool connected);
    void CloseLocked(std::thread &threadToJoin);

    mutable std::mutex _mutex;
    mutable std::mutex _callbackMutex;
    PacketCallback _onPacketReceived;
    ConnectionCallback _onConnectionChanged;

    HANDLE _deviceHandle{INVALID_HANDLE_VALUE};
    HANDLE _stopEvent{nullptr};
    std::thread _readerThread;
    std::atomic<bool> _connected{false};
    std::atomic<bool> _lowLatencyMode{false};
    uint64_t _targetAddress{0};
    void *_cmNotification{nullptr};
};

//
// Composite AAP transport that prefers the No-Test-Mode `AirpodsBridgeService` (WinUSB +
// signed usbip-win2) and transparently falls back to the optional `BthAapBridge` KMDF
// profile driver when the service is not running.
//
class CompositeControlTransport final : public ControlTransport
{
public:
    CompositeControlTransport(
        std::unique_ptr<ControlTransport> primary, std::unique_ptr<ControlTransport> fallback);
    ~CompositeControlTransport() override;

    bool IsAvailable() const override;
    bool IsConnected() const override;
    std::string BackendName() const override;

    void Connect(uint64_t bluetoothAddress) override;
    void Disconnect() override;
    bool SendPacket(std::span<const uint8_t> packet) override;
    void SetLowLatencyMode(bool enabled) override;

    std::optional<RadioServiceInfo> GetRadioServiceInfo() const override;
    bool ApplyRadioProxyOptions(const RadioProxyOptions &options) override;
    bool ConfigureVirtualMic(
        uint64_t bluetoothAddress, bool enable, bool hdVoice,
        const std::string &deviceName) override;
    bool RetryRadioServiceAfterGuard() override;
    bool RestoreNativeRadioDriver() override;

    void SetOnPacketReceived(PacketCallback callback) override;
    void SetOnConnectionChanged(ConnectionCallback callback) override;

private:
    ControlTransport *ActiveBackend() const;

    mutable std::mutex _mutex;
    std::unique_ptr<ControlTransport> _primary;
    std::unique_ptr<ControlTransport> _fallback;
    ControlTransport *_active{nullptr};
    PacketCallback _onPacket;
    ConnectionCallback _onConn;
};

} // namespace Core::AirPods
