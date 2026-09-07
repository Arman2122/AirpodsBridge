// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/AutomationPolicies.h"

namespace Core::Automation {

//////////////////////////////////////////////////

bool EarDetectionPolicy::IsWorn(const AirPods::State &state, bool singleBud)
{
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
    if (_lastAttempt.has_value() && now - _lastAttempt.value() < kCooldown) {
        return false;
    }
    _lastAttempt = now;
    return true;
}

} // namespace Core::Automation
