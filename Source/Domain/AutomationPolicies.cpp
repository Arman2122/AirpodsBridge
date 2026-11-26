// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/AutomationPolicies.h"

#include <algorithm>

namespace Core::Automation {

//////////////////////////////////////////////////

bool EarDetectionPolicy::IsWorn(const AirPods::State &state, bool singleBud)
{
    // A one-piece device (AirPods Max) reports its worn bit on whichever side broadcasts
    // the advertisement, so it never requires both sides simultaneously.
    if (AirPods::IsOnePiece(state.model)) {
        return AirPods::OnePieceInEar(state.pods);
    }
    const bool left = state.pods.left.isInEar;
    const bool right = state.pods.right.isInEar;
    return singleBud ? (left || right) : (left && right);
}

MediaAction EarDetectionPolicy::OnStateChanged(
    const std::optional<AirPods::State> &previous, const AirPods::State &current,
    const EarDetectionOptions &options)
{
    if (!options.enabled || !previous.has_value()) {
        return MediaAction::None;
    }

    const bool wasWorn = IsWorn(previous.value(), options.singleBud);
    const bool isWorn = IsWorn(current, options.singleBud);
    if (wasWorn == isWorn) {
        return MediaAction::None;
    }
    if (!isWorn) {
        return MediaAction::Pause;
    }
    return options.resumePlayback ? MediaAction::Play : MediaAction::None;
}

//////////////////////////////////////////////////

MediaAction DisconnectPolicy::OnConnectionChanged(bool connected, bool enabled)
{
    // Tracked whether or not the rule is on, so turning it on mid-session does not treat
    // the next report as a drop.
    const bool dropped = _wasConnected && !connected;
    _wasConnected = connected;
    return dropped && enabled ? MediaAction::Pause : MediaAction::None;
}

//////////////////////////////////////////////////

std::vector<BatteryAlert>
BatteryAlertPolicy::OnStateChanged(const AirPods::State &state, const BatteryAlertOptions &options)
{
    if (!options.enabled) {
        // Nothing is remembered while the rule is off, so turning it on with a battery
        // already low alerts once, straight away.
        Reset();
        return {};
    }

    std::vector<BatteryAlert> alerts;

    const auto consider = [&](BatteryComponent component,
                              const AirPods::Details::BasicState &part) {
        auto &alerted = _alerted.at(static_cast<size_t>(component));
        if (!part.battery.Available()) {
            return;
        }

        const auto level = part.battery.Value();
        if (part.isCharging || level > options.threshold + kRearmMargin) {
            alerted = false;
            return;
        }
        if (level > options.threshold || alerted) {
            return;
        }

        alerted = true;
        alerts.push_back({component, level});
    };

    if (AirPods::IsOnePiece(state.model)) {
        consider(BatteryComponent::Device, AirPods::OnePieceState(state.pods));
        return alerts;
    }

    consider(BatteryComponent::Left, state.pods.left);
    consider(BatteryComponent::Right, state.pods.right);
    consider(BatteryComponent::Case, state.caseBox);
    return alerts;
}

void BatteryAlertPolicy::Reset()
{
    _alerted.fill(false);
}

//////////////////////////////////////////////////

bool AutoConnectPolicy::OnLidOpenedNearby(bool deviceConnected, bool enabled, Clock::time_point now)
{
    if (!enabled || deviceConnected) {
        return false;
    }
    if (_disconnectedUntil.has_value() && now < _disconnectedUntil.value()) {
        return false;
    }
    if (_lastAttempt.has_value() && now - _lastAttempt.value() < kCooldown) {
        return false;
    }
    _lastAttempt = now;
    return true;
}

void AutoConnectPolicy::OnDeviceDisconnected(bool enabled, Clock::time_point now)
{
    if (enabled) {
        _disconnectedUntil = now + kPostDisconnectCooldown;
    }
}

void AutoConnectPolicy::Reset()
{
    _disconnectedUntil.reset();
}

//////////////////////////////////////////////////

bool FullChargeAlertPolicy::OnStateChanged(const AirPods::State &state, bool enabled)
{
    if (!enabled) {
        Reset();
        return false;
    }

    auto considerSingle = [&](const AirPods::Details::BasicState &part) {
        if (!part.battery.Available()) {
            return false;
        }
        const uint32_t level = part.battery.Value();
        if (level <= kRearmThreshold) {
            _armed = true;
            _alerted = false;
            return false;
        }
        if (part.isCharging && level < kFullLevel) {
            _armed = true;
            return false;
        }
        if (_armed && !_alerted && part.isCharging && level >= kFullLevel) {
            _alerted = true;
            _armed = false;
            return true;
        }
        return false;
    };

    if (AirPods::IsOnePiece(state.model)) {
        return considerSingle(AirPods::OnePieceState(state.pods));
    }

    const bool leftAvail = state.pods.left.battery.Available();
    const bool rightAvail = state.pods.right.battery.Available();
    if (!leftAvail && !rightAvail) {
        return false;
    }

    const uint32_t minLevel = [&] {
        if (leftAvail && rightAvail) {
            return std::min(state.pods.left.battery.Value(), state.pods.right.battery.Value());
        }
        return leftAvail ? state.pods.left.battery.Value() : state.pods.right.battery.Value();
    }();
    const bool anyCharging =
        (leftAvail && state.pods.left.isCharging) || (rightAvail && state.pods.right.isCharging);

    if (minLevel <= kRearmThreshold) {
        _armed = true;
        _alerted = false;
        return false;
    }
    if (anyCharging && minLevel < kFullLevel) {
        _armed = true;
        return false;
    }
    if (_armed && !_alerted && anyCharging && minLevel >= kFullLevel) {
        _alerted = true;
        _armed = false;
        return true;
    }
    return false;
}

void FullChargeAlertPolicy::Reset()
{
    _armed = false;
    _alerted = false;
}

//////////////////////////////////////////////////

AudioRouteAction SmartAudioRoutePolicy::OnStateChanged(
    const std::optional<AirPods::State> &previous, const AirPods::State &current, bool enabled,
    bool singleBud)
{
    if (!enabled) {
        _routedToAirPods = false;
        return AudioRouteAction::None;
    }

    const bool isWorn = EarDetectionPolicy::IsWorn(current, singleBud);
    if (!previous.has_value()) {
        // Connecting while already wearing the AirPods should route playback straight to
        // them without waiting for the user to take a bud out and put it back in.
        if (isWorn && !_routedToAirPods) {
            _routedToAirPods = true;
            return AudioRouteAction::SwitchToAirPods;
        }
        return AudioRouteAction::None;
    }

    const bool wasWorn = EarDetectionPolicy::IsWorn(previous.value(), singleBud);
    if (wasWorn == isWorn) {
        return AudioRouteAction::None;
    }

    if (isWorn) {
        _routedToAirPods = true;
        return AudioRouteAction::SwitchToAirPods;
    }

    if (_routedToAirPods) {
        _routedToAirPods = false;
        return AudioRouteAction::RestorePrevious;
    }
    return AudioRouteAction::None;
}

AudioRouteAction SmartAudioRoutePolicy::OnDisconnected(bool enabled)
{
    if (!enabled || !_routedToAirPods) {
        _routedToAirPods = false;
        return AudioRouteAction::None;
    }
    _routedToAirPods = false;
    return AudioRouteAction::RestorePrevious;
}

void SmartAudioRoutePolicy::Reset()
{
    _routedToAirPods = false;
}

} // namespace Core::Automation
