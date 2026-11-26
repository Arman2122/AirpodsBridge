// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

#include <QObject>
#include <QString>
#include <QTimer>

#include "Source/Domain/AirPodsState.h"
#include "Source/Domain/AutomationPolicies.h"

namespace Core::Automation {

//
// Where the engine's decisions land.
//
// The application implements this over the real media controller, tray icon and quick
// connect; tests implement it over a recording fake. It is the only way a policy's answer
// reaches the outside world, so it is also the list of everything automation can do.
//
class Effects
{
public:
    virtual ~Effects() = default;

    virtual void PlayMedia() = 0;
    virtual void PauseMedia() = 0;
    virtual void Notify(const QString &title, const QString &text) = 0;
    virtual void ConnectDevice() = 0;
    virtual void SwitchAudioOutputToAirPods() {}
    virtual void RestorePreviousAudioOutput() {}
};

//
// Feeds device events to the policies and carries their decisions out.
//
// Lives on the GUI thread. The device manager reports from its Bluetooth thread, so the
// application connects its signals to the slots below with queued connections; the
// setters are called from settings watchers, which run on the GUI thread as well. Nothing
// here needs a lock.
//
class Engine : public QObject
{
    Q_OBJECT

public:
    explicit Engine(
        Effects &effects, QObject *parent = nullptr,
        std::chrono::milliseconds bleEarDebounce = std::chrono::milliseconds{0});

    void SetEarDetectionEnabled(bool enable);
    void SetEarDetectionResumePlayback(bool enable);
    void SetEarDetectionSingleBud(bool enable);
    void SetPauseOnDisconnect(bool enable);
    void SetLowBatteryNotification(bool enable);
    void SetLowBatteryThreshold(uint32_t threshold);
    void SetFullChargeNotification(bool enable);
    void SetSmartAudioRouting(bool enable);
    void SetAutoConnectOnLidOpen(bool enable);

public Q_SLOTS:
    void OnStateUpdated(const Core::AirPods::State &state);
    void OnDisconnected();
    void OnDeviceConnectionChanged(bool connected);
    void OnLidOpenedNearby();

private:
    Effects &_effects;

    EarDetectionOptions _earDetection;
    bool _pauseOnDisconnect{true};
    BatteryAlertOptions _batteryAlert;
    bool _fullChargeNotification{false};
    bool _smartAudioRouting{false};
    bool _autoConnectOnLidOpen{false};

    bool _deviceConnected{false};
    std::chrono::milliseconds _bleEarDebounce{0};
    std::optional<AirPods::State> _lastState;
    std::optional<AirPods::State> _pendingBleWearState;
    QTimer _bleWearDebounceTimer{this};
    DisconnectPolicy _disconnectPolicy;
    BatteryAlertPolicy _batteryAlertPolicy;
    FullChargeAlertPolicy _fullChargePolicy;
    SmartAudioRoutePolicy _audioRoutePolicy;
    AutoConnectPolicy _autoConnectPolicy;

    void ApplyWearState(const AirPods::State &state);
    void Perform(MediaAction action);
    void Perform(AudioRouteAction action);
    static QString Describe(const BatteryAlert &alert, const QString &deviceName);
};

} // namespace Core::Automation
