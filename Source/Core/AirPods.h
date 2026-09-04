// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
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
class Manager : public QObject
{
    Q_OBJECT

public:
    explicit Manager(QObject *parent = nullptr);
    ~Manager();

    void StartScanner();
    void StopScanner();

    void OnRssiMinChanged(int16_t rssiMin);
    void OnAutomaticEarDetectionChanged(bool enable);
    void OnBoundDeviceAddressChanged(uint64_t address);

private:
    std::mutex _mutex;
    Details::StateManager _stateMgr;
    std::optional<Bluetooth::Device> _boundDevice;
    QString _deviceName;
    Model _boundModel{Model::Unknown};
    bool _deviceConnected{false};
    bool _scannerWanted{false};
    bool _automaticEarDetection{false};
    uint64_t _requestedDeviceAddress{0};
    std::jthread _deviceLookupThread;

    // Declared last so that it is destroyed first: its destructor stops the watcher and waits
    // for in-flight callbacks, which reach `_stateMgr` and `_mutex` above.
    Bluetooth::AdvertisementWatcher _adWatcher;

    void CompleteBoundDeviceLookup(uint64_t address, std::optional<Bluetooth::Device> device);
    void OnBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state);
    void OnStateChanged(Details::StateManager::UpdateEvent updateEvent);
    void OnLidOpened(bool opened);
    void OnBothInEar(bool isBothInEar);
    bool OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data);
    void OnAdvWatcherStateChanged(
        Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError);

Q_SIGNALS:
    void StateUpdated(const Core::AirPods::State &newState);
    void BoundDeviceUnavailable();
    void Disconnected();
    void LidToggled(bool opened);
    void ScannerAvailabilityChanged(bool available);
    void DeviceConnectionChanged(bool connected);
};

std::vector<Core::Bluetooth::Device> GetDevices();

} // namespace Core::AirPods
