// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include <QObject>

#include "Source/Core/Bluetooth.h"
#include "Source/Domain/AirPodsStateManager.h"

namespace Core::AirPods {

//
// Owns the platform-facing half of the feature: it drives the BLE scanner, tracks the
// bound device's connection state, and feeds advertisements to the domain state manager.
//
// It reports what the device is doing and decides nothing about it. What to do in
// response -- pause media, connect, notify -- is `Core::Automation::Engine`, listening to
// the signals below.
//
class Manager : public QObject
{
    Q_OBJECT

public:
    explicit Manager(QObject *parent = nullptr);
    ~Manager();

    void StartScanner();
    void StopScanner();

    void OnRssiMinChanged(int16_t rssiMin);
    void OnBoundDeviceAddressChanged(uint64_t address);

    // Keep the scanner running while the bound device is disconnected, so that its case
    // opening nearby is noticed. This costs idle CPU; see Docs/Performance.md.
    void SetScanWhileDisconnected(bool enable);

private:
    std::mutex _mutex;
    Details::StateManager _stateMgr;
    std::optional<Bluetooth::Device> _boundDevice;
    QString _deviceName;
    Model _boundModel{Model::Unknown};
    bool _deviceConnected{false};
    bool _scannerWanted{false};
    bool _scanWhileDisconnected{false};
    // Mirrors the state manager's threshold for the disconnected path, which never reaches
    // the state manager. Rejects everything until the setting arrives.
    int16_t _rssiMin{std::numeric_limits<int16_t>::max()};
    uint64_t _requestedDeviceAddress{0};
    std::jthread _deviceLookupThread;

    // Declared last so that it is destroyed first: its destructor stops the watcher and waits
    // for in-flight callbacks, which reach `_stateMgr` and `_mutex` above.
    Bluetooth::AdvertisementWatcher _adWatcher;

    bool ShouldScan() const;
    void ReconcileScannerLater();
    void CompleteBoundDeviceLookup(uint64_t address, std::optional<Bluetooth::Device> device);
    void OnBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state);
    void OnStateChanged(Details::StateManager::UpdateEvent updateEvent);
    void OnLidOpened(bool opened);
    bool OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data);
    void OnAdvertisementWhileDisconnected(const Details::Advertisement &adv);
    void OnAdvWatcherStateChanged(
        Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError);

Q_SIGNALS:
    void StateUpdated(const Core::AirPods::State &newState);
    void BoundDeviceUnavailable();
    void Disconnected();
    void LidToggled(bool opened);
    // The bound model's case opened within range while the device is not connected. Only
    // reported while scanning is kept on for that purpose, and repeats with every
    // advertisement the open case sends.
    void LidOpenedNearby();
    void ScannerAvailabilityChanged(bool available);
    void DeviceConnectionChanged(bool connected);
};

std::vector<Core::Bluetooth::Device> GetDevices();

} // namespace Core::AirPods
