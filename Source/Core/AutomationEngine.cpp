// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/AutomationEngine.h"

#include <magic_enum.hpp>

#include "Source/Support/Logger.h"

namespace Core::Automation {

Engine::Engine(Effects &effects, QObject *parent, std::chrono::milliseconds bleEarDebounce)
    : QObject{parent}, _effects{effects}, _bleEarDebounce{bleEarDebounce}
{
    _bleWearDebounceTimer.setSingleShot(true);
    connect(&_bleWearDebounceTimer, &QTimer::timeout, this, [this] {
        if (!_pendingBleWearState.has_value()) {
            return;
        }
        const auto next = _pendingBleWearState.value();
        _pendingBleWearState.reset();
        ApplyWearState(next);
    });
}

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

void Engine::SetFullChargeNotification(bool enable)
{
    _fullChargeNotification = enable;
}

void Engine::SetSmartAudioRouting(bool enable)
{
    _smartAudioRouting = enable;
    if (!enable) {
        _audioRoutePolicy.Reset();
        return;
    }
    // If the user turns smart routing on while already wearing connected AirPods, switch
    // output immediately rather than waiting for them to remove and re-insert a bud.
    if (_lastState.has_value()) {
        Perform(_audioRoutePolicy.OnStateChanged(
            std::nullopt, _lastState.value(), _smartAudioRouting, _earDetection.singleBud));
    }
}

void Engine::SetAutoConnectOnLidOpen(bool enable)
{
    _autoConnectOnLidOpen = enable;
    _autoConnectPolicy.Reset();
}

void Engine::ApplyWearState(const AirPods::State &state)
{
    const MediaAction mediaAction =
        EarDetectionPolicy::OnStateChanged(_lastState, state, _earDetection);
    const AudioRouteAction routeAction = _audioRoutePolicy.OnStateChanged(
        _lastState, state, _smartAudioRouting, _earDetection.singleBud);

    // Order effects cleanly so players never start playing on the old output only to be
    // yanked onto AirPods 50 ms later (which triggers Windows BthA2dp silent-switch glitches),
    // and never leak sound onto speakers before pausing when AirPods are removed.
    if (routeAction == AudioRouteAction::SwitchToAirPods) {
        Perform(routeAction);
        Perform(mediaAction);
    }
    else {
        Perform(mediaAction);
        Perform(routeAction);
    }
    _lastState = state;
}

void Engine::OnStateUpdated(const AirPods::State &state)
{
    for (const auto &alert : _batteryAlertPolicy.OnStateChanged(state, _batteryAlert)) {
        LOG(Info, "Low battery: {} at {}%.", magic_enum::enum_name(alert.component), alert.level);
        _effects.Notify(tr("Low battery"), Describe(alert, state.displayName));
    }

    if (_fullChargePolicy.OnStateChanged(state, _fullChargeNotification)) {
        const QString name = state.displayName.isEmpty() ? tr("Your AirPods") : state.displayName;
        LOG(Info, "Full charge notification for '{}'.", name.toStdString());
        _effects.Notify(tr("Fully charged"), tr("%1 are charged to 100%.").arg(name));
    }

    // Authoritative AAP ear detection (1% session) reacts immediately; coarse BLE broadcasts
    // can jitter between Left and Right advertisements, so debounce wear transitions briefly
    // when `_bleEarDebounce > 0` to prevent Play/Pause and output-switching storms.
    const bool isBleOnly = state.batteryPrecision != AirPods::BatteryPrecision::ExactPercent;
    if (isBleOnly && _bleEarDebounce.count() > 0 && _lastState.has_value()) {
        const bool wasWorn =
            EarDetectionPolicy::IsWorn(_lastState.value(), _earDetection.singleBud);
        const bool nowWorn = EarDetectionPolicy::IsWorn(state, _earDetection.singleBud);
        if (wasWorn != nowWorn) {
            _pendingBleWearState = state;
            if (!_bleWearDebounceTimer.isActive()) {
                _bleWearDebounceTimer.start(static_cast<int>(_bleEarDebounce.count()));
            }
            return;
        }
        // Returned to prior wear state before debounce elapsed: cancel jitter transition.
        _bleWearDebounceTimer.stop();
        _pendingBleWearState.reset();
        _lastState = state;
        return;
    }

    _bleWearDebounceTimer.stop();
    _pendingBleWearState.reset();
    ApplyWearState(state);
}

void Engine::OnDisconnected()
{
    // The state manager discarded what it knew. Whatever comes next is a fresh session:
    // no transition to react to, and every battery alert armed again.
    _bleWearDebounceTimer.stop();
    _pendingBleWearState.reset();
    _lastState.reset();
    _batteryAlertPolicy.Reset();
    _fullChargePolicy.Reset();
    Perform(_audioRoutePolicy.OnDisconnected(_smartAudioRouting));
}

void Engine::OnDeviceConnectionChanged(bool connected)
{
    const bool dropped = _deviceConnected && !connected;
    _deviceConnected = connected;
    Perform(_disconnectPolicy.OnConnectionChanged(connected, _pauseOnDisconnect));
    if (!connected) {
        _bleWearDebounceTimer.stop();
        _pendingBleWearState.reset();
        Perform(_audioRoutePolicy.OnDisconnected(_smartAudioRouting));
        if (dropped) {
            _autoConnectPolicy.OnDeviceDisconnected(
                _autoConnectOnLidOpen, AutoConnectPolicy::Clock::now());
        }
    }
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

void Engine::Perform(AudioRouteAction action)
{
    switch (action) {
    case AudioRouteAction::SwitchToAirPods:
        LOG(Info, "Automation: switching default playback output to AirPods.");
        _effects.SwitchAudioOutputToAirPods();
        break;
    case AudioRouteAction::RestorePrevious:
        LOG(Info, "Automation: restoring previous playback output.");
        _effects.RestorePreviousAudioOutput();
        break;
    case AudioRouteAction::None:
        break;
    }
}

QString Engine::Describe(const BatteryAlert &alert, const QString &deviceName)
{
    switch (alert.component) {
    case BatteryComponent::Left:
        return tr("Left AirPod is at %1%.").arg(alert.level);
    case BatteryComponent::Right:
        return tr("Right AirPod is at %1%.").arg(alert.level);
    case BatteryComponent::Case:
        return tr("The case is at %1%.").arg(alert.level);
    case BatteryComponent::Device: {
        const QString name = deviceName.isEmpty() ? tr("Your headphones") : deviceName;
        return tr("%1 is at %2%.").arg(name).arg(alert.level);
    }
    }
    return {};
}

} // namespace Core::Automation
