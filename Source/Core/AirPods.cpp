// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/AirPods.h"

#include <algorithm>
#include <utility>

#include "Source/Support/Error.h"
#include "Source/Support/Helper.h"
#include "Source/Support/Logger.h"

namespace Core::AirPods {

Manager::Manager(QObject *parent) : QObject{parent}
{
    _stateMgr.SetOnDiscardState([this] {
        QMetaObject::invokeMethod(this, [this] { emit Disconnected(); }, Qt::QueuedConnection);
    });

    _adWatcher.CbReceived() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvertisementReceived(std::forward<decltype(args)>(args)...);
    };

    _adWatcher.CbStateChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnAdvWatcherStateChanged(std::forward<decltype(args)>(args)...);
    };
}

Manager::~Manager()
{
    StopScanner();

    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }
}

bool Manager::ShouldScan() const
{
    return _boundDevice.has_value() && (_deviceConnected || _scanWhileDisconnected);
}

void Manager::ReconcileScannerLater()
{
    // Start and stop take the watcher's own locks and wait for its callbacks, so they never
    // run from inside one; the decision is re-read on the manager's thread.
    QMetaObject::invokeMethod(
        this,
        [this] {
            bool wanted;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                wanted = ShouldScan();
            }
            if (wanted) {
                StartScanner();
            }
            else {
                StopScanner();
            }
        },
        Qt::QueuedConnection);
}

void Manager::StartScanner()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!ShouldScan()) {
            LOG(Info, "Skip Bluetooth AdvWatcher start: no bound device, or it is disconnected "
                      "and scanning while disconnected is off.");
            return;
        }
        if (_scannerWanted) {
            return;
        }
        _scannerWanted = true;
    }

    if (!_adWatcher.Start()) {
        std::lock_guard<std::mutex> lock{_mutex};
        _scannerWanted = false;
        LOG(Warn, "Bluetooth AdvWatcher start failed.");
    }
    else {
        LOG(Info, "Bluetooth AdvWatcher start succeeded.");
    }
}

void Manager::StopScanner()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_scannerWanted) {
            return;
        }
        _scannerWanted = false;
    }

    if (!_adWatcher.Stop()) {
        LOG(Warn, "AsyncScanner::Stop() failed.");
    }
    else {
        LOG(Info, "AsyncScanner::Stop() succeeded.");
    }
}

void Manager::OnRssiMinChanged(int16_t rssiMin)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _rssiMin = rssiMin;
    _stateMgr.OnRssiMinChanged(rssiMin);
}

void Manager::SetScanWhileDisconnected(bool enable)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_scanWhileDisconnected == enable) {
            return;
        }
        _scanWhileDisconnected = enable;
    }

    LOG(Info, "Scan while disconnected: {}", enable);
    ReconcileScannerLater();
}

void Manager::OnBoundDeviceAddressChanged(uint64_t address)
{
    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }

    std::lock_guard<std::mutex> lock{_mutex};

    _requestedDeviceAddress = address;
    _boundDevice.reset();
    _boundModel = Model::Unknown;
    _deviceConnected = false;
    _stateMgr.Disconnect();
    emit DeviceConnectionChanged(false);
    ReconcileScannerLater();

    if (address == 0) {
        LOG(Info, "Unbind device.");
        return;
    }

    LOG(Info, "Bind a new device.");

    _deviceLookupThread = std::jthread{[this, address](std::stop_token stopToken) {
        Support::OS::Windows::Winrt::Initialize();
        auto device = Bluetooth::DeviceManager::FindDevice(address);
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, address, device = std::move(device)]() mutable {
                CompleteBoundDeviceLookup(address, std::move(device));
            },
            Qt::QueuedConnection);
    }};
}

void Manager::CompleteBoundDeviceLookup(uint64_t address, std::optional<Bluetooth::Device> device)
{
    std::lock_guard<std::mutex> lock{_mutex};

    if (address != _requestedDeviceAddress) {
        LOG(Info, "Ignore a stale bound-device lookup result.");
        return;
    }

    if (!device.has_value()) {
        LOG(Warn, "The bound device address is no longer available.");
        emit BoundDeviceUnavailable();
        return;
    }

    _boundDevice = std::move(device);
    _boundModel = AppleCP::AirPods::GetModel(_boundDevice->GetProductId());

    _deviceName = QString::fromStdString([&] {
        auto name = _boundDevice->GetName();
        return name.find("Bluetooth") != std::string::npos ? std::string{} : name;
    }());

    _boundDevice->CbConnectionStatusChanged() += [this](auto &&...args) {
        std::lock_guard<std::mutex> lock{_mutex};
        OnBoundDeviceConnectionStateChanged(std::forward<decltype(args)>(args)...);
    };

    OnBoundDeviceConnectionStateChanged(_boundDevice->GetConnectionState());
}

void Manager::OnBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state)
{
    const bool oldDeviceConnected = _deviceConnected;
    bool newDeviceConnected = state == Bluetooth::DeviceState::Connected;
    bool doDisconnect = oldDeviceConnected && !newDeviceConnected;
    _deviceConnected = newDeviceConnected;
    emit DeviceConnectionChanged(newDeviceConnected);

    ReconcileScannerLater();

    if (doDisconnect) {
        _stateMgr.Disconnect();
    }

    LOG(Info, "The device we bound is updated. current: {}, new: {}", oldDeviceConnected,
        newDeviceConnected);
}

void Manager::OnStateChanged(Details::StateManager::UpdateEvent updateEvent)
{
    const auto &oldState = updateEvent.oldState;
    auto &newState = updateEvent.newState;

    newState.displayName = Details::ResolveDisplayName(_deviceName, newState.model);

    emit StateUpdated(newState);

    bool newLidOpened = newState.caseBox.isLidOpened && newState.caseBox.isBothPodsInCase;
    bool lidStateSwitched;
    if (!oldState.has_value()) {
        lidStateSwitched = newLidOpened;
    }
    else {
        bool oldLidOpened = oldState->caseBox.isLidOpened && oldState->caseBox.isBothPodsInCase;
        lidStateSwitched = oldLidOpened != newLidOpened;
    }
    if (lidStateSwitched) {
        OnLidOpened(newLidOpened);
    }
}

void Manager::OnLidOpened(bool opened)
{
    emit LidToggled(opened);
}

bool Manager::OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    if (!Details::Advertisement::IsDesiredAdv(data)) {
        return false;
    }

    Details::Advertisement adv{data};

    if (!_deviceConnected) {
        if (_scanWhileDisconnected) {
            OnAdvertisementWhileDisconnected(adv);
        }
        else {
            LOG(Trace, "AirPods advertisement received, but device disconnected.");
        }
        return false;
    }

    if (_boundModel != Model::Unknown && adv.GetAdvState().model != _boundModel) {
        LOG(Trace, "Ignore advertisement for a model other than the bound device.");
        return false;
    }

    LOG(Trace, "AirPods advertisement received. Data: {}, Address Hash: {}, RSSI: {}",
        Helper::ToString(adv.GetDesensitizedData()), Helper::Hash(data.address), data.rssi);

    auto optUpdateEvent = _stateMgr.OnAdvReceived(std::move(adv));
    if (optUpdateEvent.has_value()) {
        OnStateChanged(std::move(optUpdateEvent.value()));
    }
    return true;
}

void Manager::OnAdvertisementWhileDisconnected(const Details::Advertisement &adv)
{
    // Random addresses make the bound pair unidentifiable, so this is the same heuristic
    // the state manager uses: the right model, close enough. Nothing here reaches the
    // state manager -- the device is not connected, and the popup must not light up.
    const auto &state = adv.GetAdvState();
    if (_boundModel != Model::Unknown && state.model != _boundModel) {
        return;
    }
    if (adv.GetRssi() < _rssiMin) {
        return;
    }
    if (!state.caseBox.isLidOpened || !state.caseBox.isBothPodsInCase) {
        return;
    }

    LOG(Trace, "The bound model's case is open nearby while disconnected. RSSI: {}", adv.GetRssi());
    emit LidOpenedNearby();
}

void Manager::OnAdvWatcherStateChanged(
    Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError)
{
    switch (state) {
    case Bluetooth::AdvertisementWatcher::State::Started:
        emit ScannerAvailabilityChanged(true);
        LOG(Info, "Bluetooth AdvWatcher started.");
        break;

    case Bluetooth::AdvertisementWatcher::State::Stopped:
        if (_scannerWanted) {
            emit ScannerAvailabilityChanged(false);
            LOG(Warn, "Bluetooth AdvWatcher stopped. Error: '{}'.", optError.value_or("nullopt"));
        }
        else {
            LOG(Info, "Bluetooth AdvWatcher stopped intentionally.");
        }
        break;

    default:
        FatalError("Unhandled adv watcher state: '{}'", Helper::ToUnderlying(state));
    }
}

std::vector<Bluetooth::Device> GetDevices()
{
    std::vector<Bluetooth::Device> devices =
        Bluetooth::DeviceManager::GetDevicesByState(Bluetooth::DeviceState::Paired);

    LOG(Info, "Paired devices count: {}", devices.size());

    devices.erase(
        std::remove_if(
            devices.begin(), devices.end(),
            [](const auto &device) {
                const auto vendorId = device.GetVendorId();
                const auto productId = device.GetProductId();
                const auto doErase = vendorId != AppleCP::VendorId ||
                                     AppleCP::AirPods::GetModel(productId) == Model::Unknown;

                LOG(Trace, "Device VendorId: '{}', ProductId: '{}', doErase: {}", vendorId,
                    productId, doErase);
                return doErase;
            }),
        devices.end());

    LOG(Info, "AirPods devices count: {} (filtered)", devices.size());
    return devices;
}

} // namespace Core::AirPods
