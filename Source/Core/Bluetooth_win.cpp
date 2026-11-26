// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/Bluetooth_win.h"

#include <objbase.h>

#include <chrono>
#include <thread>
#include <unordered_map>

#include "Source/Core/Debug.h"
#include "Source/Domain/AppleCP.h"
#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"

namespace Core::Bluetooth {

using namespace WinrtFoundation;
using namespace WinrtBluetooth;
using namespace WinrtBluetoothAdv;
using namespace WinrtDevicesEnumeration;

using namespace Core::Debug;

namespace {

std::string GuidToCanonicalString(const winrt::guid &guid)
{
    GUID native{
        guid.Data1,
        guid.Data2,
        guid.Data3,
        {
            guid.Data4[0],
            guid.Data4[1],
            guid.Data4[2],
            guid.Data4[3],
            guid.Data4[4],
            guid.Data4[5],
            guid.Data4[6],
            guid.Data4[7],
        },
    };
    wchar_t buffer[39]{};
    if (StringFromGUID2(native, buffer, 39) == 0) {
        return {};
    }
    return QString::fromWCharArray(buffer).toStdString();
}

} // namespace

//////////////////////////////////////////////////
// Device
//

Device::Device(BluetoothDevice device) : _device{std::move(device)}
{
    RegisterHandlers();
}

Device::Device(const Device &rhs)
{
    CopyFrom(rhs);
}

Device::Device(Device &&rhs) noexcept
{
    MoveFrom(std::move(rhs));
}

Device::~Device()
{
    UnregisterHandlers();
}

Device &Device::operator=(const Device &rhs)
{
    if (this != &rhs) {
        CopyFrom(rhs);
    }
    return *this;
}

Device &Device::operator=(Device &&rhs) noexcept
{
    if (this != &rhs) {
        MoveFrom(std::move(rhs));
    }
    return *this;
}

uint64_t Device::GetAddress() const
{
    return _device->BluetoothAddress();
}

std::string Device::GetName() const
{
    return winrt::to_string(_device->Name());
}

uint16_t Device::GetVendorId() const
{
    return GetProperty<uint16_t>(kPropertyBluetoothVendorId, 0);
}

uint16_t Device::GetProductId() const
{
    return GetProperty<uint16_t>(kPropertyBluetoothProductId, 0);
}

DeviceState Device::GetConnectionState() const
{
    return _device->ConnectionStatus() == BluetoothConnectionStatus::Connected
               ? DeviceState::Connected
               : DeviceState::Disconnected;
}

std::string Device::GetContainerId() const
{
    try {
        const auto &optInfo = GetInfo();
        if (!optInfo.has_value()) {
            return {};
        }

        const auto boxed = optInfo->Properties().TryLookup(kPropertyAepContainerId);
        if (!boxed) {
            return {};
        }

        try {
            return winrt::to_string(winrt::unbox_value<winrt::hstring>(boxed));
        }
        catch (const Support::OS::Windows::Winrt::Exception &) {
        }

        try {
            return GuidToCanonicalString(winrt::unbox_value<winrt::guid>(boxed));
        }
        catch (const Support::OS::Windows::Winrt::Exception &) {
        }
    }
    catch (const Support::OS::Windows::Winrt::Exception &ex) {
        LOG(Warn, "GetContainerId() failed. {}", Helper::ToString(ex));
    }
    return {};
}

void Device::RegisterHandlers()
{
    UnregisterHandlers();

    if (!_device.has_value()) {
        return;
    }

    _tokenConnectionStatusChanged = _device->ConnectionStatusChanged(
        [this](const BluetoothDevice &sender, IInspectable) { OnConnectionStatusChanged(sender); });

    _tokenNameChanged = _device->NameChanged(
        [this](const BluetoothDevice &sender, IInspectable) { OnNameChanged(sender); });
}

void Device::UnregisterHandlers()
{
    if (!_device.has_value()) {
        _tokenConnectionStatusChanged = {};
        _tokenNameChanged = {};
        return;
    }

    if (_tokenConnectionStatusChanged) {
        _device->ConnectionStatusChanged(_tokenConnectionStatusChanged);
        _tokenConnectionStatusChanged = {};
    }

    if (_tokenNameChanged) {
        _device->NameChanged(_tokenNameChanged);
        _tokenNameChanged = {};
    }
}

void Device::CopyFrom(const Device &rhs)
{
    UnregisterHandlers();
    _device = rhs._device;
    _info = rhs._info;
    RegisterHandlers();
}

void Device::MoveFrom(Device &&rhs) noexcept
{
    UnregisterHandlers();
    rhs.UnregisterHandlers();
    _device = std::move(rhs._device);
    _info = std::move(rhs._info);
    rhs._device.reset();
    rhs._info.reset();
    RegisterHandlers();
}

const std::optional<DeviceInformation> &Device::GetInfo() const
{
    if (_info.has_value() || !_device.has_value()) {
        return _info;
    }

    // The blocking `get()` runs on a dedicated thread rather than the caller's. `GetProductId()`
    // and `GetVendorId()` are reached from the GUI thread - `CompleteBoundDeviceLookup` arrives
    // there by queued connection, and `MainWindow::ShowDeviceSelector` runs there outright - and
    // QApplication puts that thread in an STA, where blocking on a WinRT async asserts in debug
    // and can deadlock in release because the completion has to marshal back into the blocked
    // apartment.
    std::thread{[this]() {
        Support::OS::Windows::Winrt::Initialize();
        try {
            // clang-format off
            _info = DeviceInformation::CreateFromIdAsync(
                _device->DeviceInformation().Id(),
                {
                    kPropertyBluetoothProductId, // uint16
                    kPropertyBluetoothVendorId,  // uint16
                    kPropertyAepContainerId,     // guid or hstring
                }
            ).get();
            // clang-format on
        }
        catch (const Support::OS::Windows::Winrt::Exception &ex) {
            LOG(Warn, "DeviceInformation::CreateFromIdAsync() failed. {}", Helper::ToString(ex));
        }
    }}.join();

    return _info;
}

void Device::OnConnectionStatusChanged(const BluetoothDevice &sender)
{
    Q_UNUSED(sender);
    CbConnectionStatusChanged().Invoke(GetConnectionState());
}

void Device::OnNameChanged(const BluetoothDevice &sender)
{
    Q_UNUSED(sender);
    CbNameChanged().Invoke(GetName());
}

//////////////////////////////////////////////////
// DevicesManager
//

namespace Details {
class DeviceManager final : public Helper::Singleton<DeviceManager>,
                            Details::DeviceManagerAbstract<Device>
{
protected:
    DeviceManager() = default;
    friend Helper::Singleton<DeviceManager>;

public:
    std::vector<Device> GetDevicesByState(DeviceState state) const override
    {
        std::vector<Device> result;

        try {
            winrt::hstring aqsString;

            switch (state) {
            case Core::Bluetooth::DeviceState::Paired:
                aqsString = BluetoothDevice::GetDeviceSelectorFromPairingState(true);
                break;
            case Core::Bluetooth::DeviceState::Disconnected:
                aqsString = BluetoothDevice::GetDeviceSelectorFromConnectionStatus(
                    BluetoothConnectionStatus::Disconnected);
                break;
            case Core::Bluetooth::DeviceState::Connected:
                aqsString = BluetoothDevice::GetDeviceSelectorFromConnectionStatus(
                    BluetoothConnectionStatus::Connected);
                break;
            default:
                APB_ASSERT(false);
                break;
            }

            auto collection =
                WinrtDevicesEnumeration::DeviceInformation::FindAllAsync(aqsString).get();

            result.reserve(collection.Size());

            for (uint32_t i = 0; i < collection.Size(); ++i) {
                const auto &deviceInfo = collection.GetAt(i);

                try {
                    auto device = BluetoothDevice::FromIdAsync(deviceInfo.Id()).get();
                    result.emplace_back(std::move(device));
                }
                catch (const Support::OS::Windows::Winrt::Exception &ex) {
                    LOG(Warn, "BluetoothDevice::FromIdAsync() failed. {}", Helper::ToString(ex));
                }
            }

            return result;
        }
        catch (const Support::OS::Windows::Winrt::Exception &ex) {
            LOG(Warn, "GetDevicesByState() failed. {}", Helper::ToString(ex));
            return result;
        }
    }

    std::optional<Device> FindDevice(uint64_t address) const override
    {
        auto devices = GetDevicesByState(Bluetooth::DeviceState::Paired);
        for (const auto &device : devices) {
            if (device.GetAddress() == address) {
                (void)device.GetProductId();
                return device;
            }
        }
        return std::nullopt;
    }
};
} // namespace Details

namespace DeviceManager {

std::vector<Device> GetDevicesByState(DeviceState state)
{
    return Details::DeviceManager::GetInstance().GetDevicesByState(state);
}

std::optional<Device> FindDevice(uint64_t address)
{
    return Details::DeviceManager::GetInstance().FindDevice(address);
}
} // namespace DeviceManager

//////////////////////////////////////////////////
// AdvertisementWatcher
//

AdvertisementWatcher::AdvertisementWatcher()
{
    // Passive scanning avoids sending SCAN_REQ packets to every device nearby.
    _bleWatcher.ScanningMode(BluetoothLEScanningMode::Passive);

#if !defined APB_DEBUG
    // Push company-ID filtering down to the Windows Bluetooth stack so non-Apple BLE
    // traffic never wakes this process across the WinRT ABI. Debug builds keep all
    // packets open so `DebugConfig::GetOverrideAdv` works without real Apple hardware.
    try {
        BluetoothLEManufacturerData appleFilter;
        appleFilter.CompanyId(AppleCP::VendorId);
        _bleWatcher.AdvertisementFilter().Advertisement().ManufacturerData().Append(appleFilter);
    }
    catch (const Support::OS::Windows::Winrt::Exception &ex) {
        LOG(Warn, "Configure BLE company filter failed: {}", Helper::ToString(ex));
    }
#endif

    auto lifetime = _lifetime;
    _receivedToken =
        _bleWatcher.Received([this, lifetime](
                                 const BluetoothLEAdvertisementWatcher &,
                                 const BluetoothLEAdvertisementReceivedEventArgs &args) {
            std::lock_guard<std::mutex> guard{lifetime->mutex};
            if (!lifetime->alive) {
                return;
            }
            OnReceived(args);
        });

    _stoppedToken =
        _bleWatcher.Stopped([this, lifetime](
                                const BluetoothLEAdvertisementWatcher &,
                                const BluetoothLEAdvertisementWatcherStoppedEventArgs &args) {
            std::lock_guard<std::mutex> guard{lifetime->mutex};
            if (!lifetime->alive) {
                return;
            }
            OnStopped(args);
        });
}

AdvertisementWatcher::~AdvertisementWatcher()
{
    _wanted = false;
    CancelRetry();

    // Revoke WinRT handlers first, then mark dead under `_lifetime->mutex` so any callback
    // that entered right before revocation finishes before our members are destroyed.
    try {
        if (_receivedToken) {
            _bleWatcher.Received(_receivedToken);
            _receivedToken = {};
        }
        if (_stoppedToken) {
            _bleWatcher.Stopped(_stoppedToken);
            _stoppedToken = {};
        }
        _bleWatcher.Stop();
    }
    catch (const Support::OS::Windows::Winrt::Exception &) {
    }

    {
        std::lock_guard<std::mutex> guard{_lifetime->mutex};
        _lifetime->alive = false;
    }
}

bool AdvertisementWatcher::Start()
{
    _wanted = true;
    CancelRetry();
    return StartInternal();
}

bool AdvertisementWatcher::StartInternal()
{
    try {
        _lastStartTime = std::chrono::steady_clock::now();

        {
            std::lock_guard<std::mutex> lock{_mutex};
            _bleWatcher.Start();
        }
        LOG(Info, "Bluetooth AdvWatcher start succeeded.");
        CbStateChanged().Invoke(State::Started, std::nullopt);
        return true;
    }
    catch (const Support::OS::Windows::Winrt::Exception &ex) {
        LOG(Warn, "Start adv watcher exception: {}", Helper::ToString(ex));
        return false;
    }
}

bool AdvertisementWatcher::Stop()
{
    _wanted = false;
    CancelRetry();

    try {
        std::lock_guard<std::mutex> lock{_mutex};
        _bleWatcher.Stop();
        LOG(Info, "Bluetooth AdvWatcher stop succeeded.");
        return true;
    }
    catch (const Support::OS::Windows::Winrt::Exception &ex) {
        LOG(Warn, "Stop adv watcher exception: {}", Helper::ToString(ex));
        return false;
    }
}

void AdvertisementWatcher::ScheduleRetry()
{
    CancelRetry();

    if (!_wanted.load()) {
        return;
    }

    _retryThread = std::jthread{[this](std::stop_token stopToken) {
        Support::OS::Windows::Winrt::Initialize();
        while (!stopToken.stop_requested() && _wanted.load()) {
            const auto target = _lastStartTime.load() + kRetryInterval;
            {
                std::unique_lock<std::mutex> lock{_retryMutex};
                _retryConVar.wait_until(
                    lock, target, [&] { return stopToken.stop_requested() || !_wanted.load(); });
            }
            if (stopToken.stop_requested() || !_wanted.load()) {
                return;
            }
            if (StartInternal()) {
                return;
            }
        }
    }};
}

void AdvertisementWatcher::CancelRetry()
{
    _retryThread.request_stop();
    _retryConVar.notify_all();
    if (_retryThread.joinable()) {
        _retryThread.join();
    }
}

void AdvertisementWatcher::OnReceived(const BluetoothLEAdvertisementReceivedEventArgs &args)
{
    ReceivedData receivedData;

    receivedData.rssi = args.RawSignalStrengthInDBm();
    receivedData.timestamp = std::chrono::steady_clock::now();
    receivedData.address = args.BluetoothAddress();

    const auto &manufacturerDataArray = args.Advertisement().ManufacturerData();
    for (uint32_t i = 0; i < manufacturerDataArray.Size(); ++i) {
        const auto &manufacturerData = manufacturerDataArray.GetAt(i);
        const auto companyId = manufacturerData.CompanyId();

#if defined APB_DEBUG
        auto overrideAdv = DebugConfig::GetInstance().GetOverrideAdv();
        if (overrideAdv.has_value()) {
            LOG(Trace, "Adv override: {}", Helper::ToString(overrideAdv.value()));
            receivedData.manufacturerDataMap.insert_or_assign(
                AppleCP::VendorId, std::move(overrideAdv.value()));
            break;
        }
#endif

        if (companyId != AppleCP::VendorId) {
            continue;
        }

        const auto &data = manufacturerData.Data();
        receivedData.manufacturerDataMap.try_emplace(
            companyId, data.data(), data.data() + data.Length());
    }

    if (receivedData.manufacturerDataMap.empty()) {
        return;
    }

    CbReceived().Invoke(receivedData);
}

void AdvertisementWatcher::OnStopped(const BluetoothLEAdvertisementWatcherStoppedEventArgs &args)
{
    static const std::unordered_map<BluetoothError, std::string> errorReasons = {
        {BluetoothError::Success, "Success"},
        {BluetoothError::RadioNotAvailable, "RadioNotAvailable"},
        {BluetoothError::ResourceInUse, "ResourceInUse"},
        {BluetoothError::DeviceNotConnected, "DeviceNotConnected"},
        {BluetoothError::OtherError, "OtherError"},
        {BluetoothError::DisabledByPolicy, "DisabledByPolicy"},
        {BluetoothError::NotSupported, "NotSupported"},
        {BluetoothError::DisabledByUser, "DisabledByUser"},
        {BluetoothError::ConsentRequired, "ConsentRequired"},
        {BluetoothError::TransportNotSupported, "TransportNotSupported"},
    };

    BluetoothLEAdvertisementWatcherStatus status;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        status = _bleWatcher.Status();
    }

    const auto errorCode = args.Error();
    std::optional<std::string> optError;

    if (errorCode == BluetoothError::Success &&
        status != BluetoothLEAdvertisementWatcherStatus::Aborted)
    {
        optError = std::nullopt;
    }
    else {
        std::string info;
        if (status == BluetoothLEAdvertisementWatcherStatus::Aborted) {
            info = "Aborted";
        }
        else if (const auto reasonIter = errorReasons.find(errorCode);
                 reasonIter != errorReasons.end())
        {
            info = reasonIter->second;
        }
        else {
            info = "Unknown error (" + std::to_string(static_cast<uint32_t>(errorCode)) + ")";
        }
        optError = std::move(info);
    }

    CbStateChanged().Invoke(State::Stopped, optError);

    // Retry asynchronously off the WinRT callback thread when the watcher stopped
    // unexpectedly (e.g. radio toggled off and back on).
    if (_wanted.load()) {
        ScheduleRetry();
    }
}
} // namespace Core::Bluetooth
