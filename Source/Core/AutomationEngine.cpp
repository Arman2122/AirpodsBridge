// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/AutomationEngine.h"

#include <magic_enum.hpp>

#include "Source/Support/Logger.h"

namespace Core::Automation {

Engine::Engine(Effects &effects, QObject *parent) : QObject{parent}, _effects{effects} {}

void Engine::SetEarDetectionEnabled(bool enable)
{
    _earDetection.enabled = enable;
}

void Engine::SetEarDetectionResumePlayback(bool enable)
{
    _earDetection.resumePlayback = enable;
}

void Engine::SetEarDetectionSingleBud(bool enable)
{
    _earDetection.singleBud = enable;
}

void Engine::SetPauseOnDisconnect(bool enable)
{
    _pauseOnDisconnect = enable;
}

void Engine::SetLowBatteryNotification(bool enable)
{
    _batteryAlert.enabled = enable;
}

void Engine::SetLowBatteryThreshold(uint32_t threshold)
{
    _batteryAlert.threshold = threshold;
}

void Engine::SetAutoConnectOnLidOpen(bool enable)
{
    _autoConnectOnLidOpen = enable;
}

void Engine::OnStateUpdated(const AirPods::State &state)
{
    Perform(EarDetectionPolicy::OnStateChanged(_lastState, state, _earDetection));

    for (const auto &alert : _batteryAlertPolicy.OnStateChanged(state, _batteryAlert)) {
        LOG(Info, "Low battery: {} at {}%.", magic_enum::enum_name(alert.component), alert.level);
        _effects.Notify(tr("Low battery"), Describe(alert));
    }

    _lastState = state;
}

void Engine::OnDisconnected()
{
    // The state manager discarded what it knew. Whatever comes next is a fresh session:
    // no transition to react to, and every battery alert armed again.
    _lastState.reset();
    _batteryAlertPolicy.Reset();
}

void Engine::OnDeviceConnectionChanged(bool connected)
{
    _deviceConnected = connected;
    Perform(_disconnectPolicy.OnConnectionChanged(connected, _pauseOnDisconnect));
}

void Engine::OnLidOpenedNearby()
{
    const bool connect = _autoConnectPolicy.OnLidOpenedNearby(
        _deviceConnected, _autoConnectOnLidOpen, AutoConnectPolicy::Clock::now());
    if (connect) {
        LOG(Info, "The case opened nearby while disconnected. Requesting a connection.");
        _effects.ConnectDevice();
    }
}

void Engine::Perform(MediaAction action)
{
    switch (action) {
    case MediaAction::Pause:
        LOG(Info, "Automation: pause media.");
        _effects.PauseMedia();
        break;
    case MediaAction::Play:
        LOG(Info, "Automation: play media.");
        _effects.PlayMedia();
        break;
    case MediaAction::None:
        break;
    }
}

QString Engine::Describe(const BatteryAlert &alert)
{
    switch (alert.component) {
    case BatteryComponent::Left:
        return tr("Left AirPod is at %1%.").arg(alert.level);
    case BatteryComponent::Right:
        return tr("Right AirPod is at %1%.").arg(alert.level);
    case BatteryComponent::Case:
        return tr("The case is at %1%.").arg(alert.level);
    }
    return {};
}

} // namespace Core::Automation
