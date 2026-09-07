// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <optional>

#include <QObject>
#include <QString>

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
    explicit Engine(Effects &effects, QObject *parent = nullptr);

    void SetEarDetectionEnabled(bool enable);
    void SetEarDetectionResumePlayback(bool enable);
    void SetEarDetectionSingleBud(bool enable);
    void SetPauseOnDisconnect(bool enable);
    void SetLowBatteryNotification(bool enable);
    void SetLowBatteryThreshold(uint32_t threshold);
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
    bool _autoConnectOnLidOpen{false};

    bool _deviceConnected{false};
    std::optional<AirPods::State> _lastState;
    DisconnectPolicy _disconnectPolicy;
    BatteryAlertPolicy _batteryAlertPolicy;
    AutoConnectPolicy _autoConnectPolicy;

    void Perform(MediaAction action);
    static QString Describe(const BatteryAlert &alert);
};

} // namespace Core::Automation
