// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#if !defined APB_OS_WIN
    #error "This file shouldn't be compiled."
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <winrt/Windows.Devices.Bluetooth.Advertisement.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Networking.h>
#include <winrt/Windows.Storage.Streams.h>

#include "Source/Core/Bluetooth_abstract.h"
#include "Source/Support/OS/Windows.h"

namespace Core::Bluetooth {

using namespace std::chrono_literals;

namespace WinrtFoundation = winrt::Windows::Foundation;
namespace WinrtBluetooth = winrt::Windows::Devices::Bluetooth;
namespace WinrtBluetoothAdv = winrt::Windows::Devices::Bluetooth::Advertisement;
namespace WinrtDevicesEnumeration = winrt::Windows::Devices::Enumeration;

class Device final : public Details::DeviceAbstract<uint64_t>
{
public:
    Device(WinrtBluetooth::BluetoothDevice device);
    Device(const Device &rhs);
    Device(Device &&rhs) noexcept;
    ~Device();

    Device &operator=(const Device &rhs);
    Device &operator=(Device &&rhs) noexcept;

    uint64_t GetAddress() const override;
    std::string GetName() const override;
    uint16_t GetVendorId() const override;
    uint16_t GetProductId() const override;
    DeviceState GetConnectionState() const override;
    std::string GetContainerId() const override;

private:
    constexpr static auto kPropertyBluetoothVendorId = L"System.DeviceInterface.Bluetooth.VendorId";
    constexpr static auto kPropertyBluetoothProductId =
        L"System.DeviceInterface.Bluetooth.ProductId";
    constexpr static auto kPropertyAepContainerId = L"System.Devices.Aep.ContainerId";

    std::optional<WinrtBluetooth::BluetoothDevice> _device;
    mutable std::optional<WinrtDevicesEnumeration::DeviceInformation> _info;
    winrt::event_token _tokenConnectionStatusChanged{}, _tokenNameChanged{};

    void RegisterHandlers();
    void UnregisterHandlers();
    void CopyFrom(const Device &rhs);
    void MoveFrom(Device &&rhs) noexcept;

    const std::optional<WinrtDevicesEnumeration::DeviceInformation> &GetInfo() const;

    template <class T>
    inline T GetProperty(const winrt::hstring &name, const T &defaultValue) const
    {
        try {
            const auto &optInfo = GetInfo();
            if (!optInfo.has_value()) {
                LOG(Warn, "optInfo.has_value() false.");
                return defaultValue;
            }

            const auto boxed = optInfo->Properties().TryLookup(name);
            return winrt::unbox_value_or<T>(boxed, defaultValue);
        }
        catch (const Support::OS::Windows::Winrt::Exception &ex) {
            LOG(Warn, "GetProperty() failed. {}", Helper::ToString(ex));
        }
        return defaultValue;
    }

    void OnConnectionStatusChanged(const WinrtBluetooth::BluetoothDevice &sender);
    void OnNameChanged(const WinrtBluetooth::BluetoothDevice &sender);
};

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state);
std::optional<Device> FindDevice(uint64_t address);

} // namespace DeviceManager

class AdvertisementWatcher final
    : public Details::AdvertisementWatcherAbstract<AdvertisementWatcher>
{
public:
    using Timestamp = ReceivedAdvertisement::Timestamp;

    explicit AdvertisementWatcher();
    ~AdvertisementWatcher();

    bool Start() override;
    bool Stop() override;

private:
    static constexpr inline auto kRetryInterval = 3s;

    // Shared life-guard so any WinRT callback still racing with teardown checks `alive`
    // under `mutex` and exits without touching destroyed members.
    struct Lifetime {
        std::mutex mutex;
        bool alive{true};
    };

    std::shared_ptr<Lifetime> _lifetime{std::make_shared<Lifetime>()};
    WinrtBluetoothAdv::BluetoothLEAdvertisementWatcher _bleWatcher;
    winrt::event_token _receivedToken{};
    winrt::event_token _stoppedToken{};
    std::mutex _mutex;

    std::atomic<bool> _wanted{false};
    std::atomic<std::chrono::steady_clock::time_point> _lastStartTime{};
    std::mutex _retryMutex;
    std::condition_variable _retryConVar;
    std::jthread _retryThread;

    bool StartInternal();
    void ScheduleRetry();
    void CancelRetry();
    void OnReceived(const WinrtBluetoothAdv::BluetoothLEAdvertisementReceivedEventArgs &args);
    void OnStopped(const WinrtBluetoothAdv::BluetoothLEAdvertisementWatcherStoppedEventArgs &args);
};
} // namespace Core::Bluetooth
