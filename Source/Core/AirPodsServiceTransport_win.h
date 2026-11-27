// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "Source/Core/AirPodsTransport.h"
#include "Source/Support/OS/Windows.h"

namespace Core::AirPods {

//
// User-mode Named Pipe client for the native x64 `AirpodsBridgeService` (No Test Mode,
// Secure Boot ON).
//
// Zero idle CPU when the service is not installed or stopped: connects to
// `\\.\pipe\AirpodsBridge.RadioService.v1` only when SCM reports `AirpodsBridgeService` as
// running (or when Connect/QueryStatus is explicitly requested), and uses a single blocking
// overlapped read loop while attached.
//
class ServiceControlTransport final : public ControlTransport
{
public:
    ServiceControlTransport();
    ~ServiceControlTransport() override;

    ServiceControlTransport(const ServiceControlTransport &) = delete;
    ServiceControlTransport &operator=(const ServiceControlTransport &) = delete;

    struct SetupOutcome {
        bool succeeded{false};
        bool rebootRequired{false};
        bool cancelled{false};
        std::string message;
    };
    using SetupProgressCallback = std::function<void(int percent, std::string message)>;
    using SetupCallback = std::function<void(SetupOutcome)>;

    static bool IsServiceInstalled();
    static bool IsServiceRunning();
    static void RunElevatedSetupAsync(
        bool uninstall, bool restoreOnly, SetupCallback onComplete,
        SetupProgressCallback onProgress = {});

    bool IsAvailable() const override;
    bool IsConnected() const override;
    std::string BackendName() const override
    {
        return "radio_service";
    }

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
    void SetOnRadioStatusChanged(RadioStatusCallback callback) override;

private:
    bool EnsurePipeConnected();
    void ClosePipeLocked(std::thread &readerToJoin);
    bool SendRawFrame(uint16_t messageType, std::span<const uint8_t> payload);
    void ReaderLoop(HANDLE pipeHandle, HANDLE stopEvent);
    void HandleInboundFrame(uint16_t rawType, std::span<const uint8_t> payload);
    void NotifyConnectionChanged(bool connected);
    void NotifyRadioStatusChanged(RadioServiceInfo info);

    mutable std::mutex _mutex;
    mutable std::mutex _writeMutex;
    mutable std::mutex _callbackMutex;
    PacketCallback _onPacketReceived;
    ConnectionCallback _onConnectionChanged;
    RadioStatusCallback _onRadioStatusChanged;

    HANDLE _pipeHandle{INVALID_HANDLE_VALUE};
    HANDLE _stopEvent{nullptr};
    std::thread _readerThread;

    std::atomic<bool> _pipeConnected{false};
    std::atomic<bool> _aapConnected{false};
    uint64_t _targetAddress{0};
    RadioProxyOptions _options{};
    RadioServiceInfo _lastInfo{};
};

} // namespace Core::AirPods
