// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Core::AirPods {

struct RadioProxyOptions {
    bool lowLatencyQos{true};
    uint32_t pollLatencyUs{25000};
    bool declineHfp{false};
    bool virtualMicEnabled{false};

    bool operator==(const RadioProxyOptions &) const = default;
};

struct RadioServiceInfo {
    bool serviceInstalled{false};
    bool serviceRunning{false};
    bool pipeConnected{false};
    bool usbipDriverReady{false};
    bool winUsbBound{false};
    bool virtualAdapterAttached{false};
    bool guardTripped{false};
    bool callAudioActive{false};
    bool virtualMicAttached{false};
    bool virtualMicCapturing{false};
    bool lowLatencyQosEnabled{true};
    bool declineHfpEnabled{false};
    uint16_t adapterVid{0};
    uint16_t adapterPid{0};
    uint16_t hciManufacturer{0};
    uint32_t activeAapChannels{0};
    uint32_t activePollLatencyUs{0};
    uint32_t recentFailures{0};
    uint64_t injectedPackets{0};
    uint64_t interceptedPackets{0};
    std::string stateName{"stopped"};
    std::string adapterDisplayName;
    std::string adapterInstanceId;
    std::string detail;

    bool operator==(const RadioServiceInfo &) const = default;
};

//
// Two-way control transport seam for the Apple Accessory Protocol (AAP over BR/EDR L2CAP,
// PSM 0x1001).
//
// Supported Windows backends:
//   1. `ServiceControlTransport` (No Test Mode, Secure Boot stays ON): communicates with
//      the native x64 `AirpodsBridgeService` (inbox WinUSB + Microsoft-signed usbip-win2 +
//      user-mode HCI/L2CAP MITM proxy).
//   2. `L2capControlTransport` (optional legacy fallback): communicates directly with the
//      KMDF `BthAapBridge` profile driver when installed.
//
class ControlTransport
{
public:
    using PacketCallback = std::function<void(std::vector<uint8_t>)>;
    using ConnectionCallback = std::function<void(bool connected)>;

    virtual ~ControlTransport() = default;

    // True when the backing service or driver is installed and usable on this machine.
    virtual bool IsAvailable() const = 0;
    virtual bool IsConnected() const = 0;
    virtual std::string BackendName() const
    {
        return "none";
    }

    virtual void Connect(uint64_t bluetoothAddress) = 0;
    virtual void Disconnect() = 0;
    virtual bool SendPacket(std::span<const uint8_t> packet) = 0;
    virtual void SetLowLatencyMode(bool) {}

    // Optional Radio Service controls (safe no-ops on non-service transports).
    virtual std::optional<RadioServiceInfo> GetRadioServiceInfo() const
    {
        return std::nullopt;
    }
    virtual bool ApplyRadioProxyOptions(const RadioProxyOptions &)
    {
        return false;
    }
    virtual bool ConfigureVirtualMic(uint64_t, bool, bool, const std::string &)
    {
        return false;
    }
    virtual bool RetryRadioServiceAfterGuard()
    {
        return false;
    }
    virtual bool RestoreNativeRadioDriver()
    {
        return false;
    }

    virtual void SetOnPacketReceived(PacketCallback callback) = 0;
    virtual void SetOnConnectionChanged(ConnectionCallback callback) = 0;
};

//
// Default transport when no AAP service or driver is installed: zero threads, zero I/O.
//
class NullControlTransport final : public ControlTransport
{
public:
    bool IsAvailable() const override
    {
        return false;
    }
    bool IsConnected() const override
    {
        return false;
    }
    void Connect(uint64_t) override {}
    void Disconnect() override {}
    bool SendPacket(std::span<const uint8_t>) override
    {
        return false;
    }
    void SetOnPacketReceived(PacketCallback) override {}
    void SetOnConnectionChanged(ConnectionCallback) override {}
};

} // namespace Core::AirPods
