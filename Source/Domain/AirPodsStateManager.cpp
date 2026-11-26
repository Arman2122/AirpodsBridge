// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/AirPodsStateManager.h"

#include <algorithm>
#include <cmath>
#include <type_traits>

#include "Source/Support/Logger.h"

namespace Core::AirPods::Details {

std::optional<State> StateManager::GetCurrentState() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _cachedState;
}

auto StateManager::OnAdvReceived(Advertisement adv, Timestamp now) -> std::optional<UpdateEvent>
{
    std::lock_guard<std::mutex> lock{_mutex};

    if (!IsPossibleDesiredAdv(adv)) {
        LOG(Warn, "This adv may not be broadcast from the device we desire.");
        return std::nullopt;
    }

    UpdateAdv(std::move(adv), now);
    return UpdateState(now);
}

auto StateManager::OnAapMessage(
    const Aap::InboundMessage &message, Model fallbackModel, Timestamp now)
    -> std::optional<UpdateEvent>
{
    std::lock_guard<std::mutex> lock{_mutex};

    State nextState = _cachedState.value_or(State{});
    if (nextState.model == Model::Unknown && fallbackModel != Model::Unknown) {
        nextState.model = fallbackModel;
    }
    nextState.audioProfile = _audioProfile;
    nextState.audioProfileSource = _audioProfileSource;
    if (_aapConnected) {
        nextState.noiseControl.controllable =
            CapabilitiesFor(nextState.model).SupportsNoiseControl();
    }

    const bool changed = Aap::ApplyInboundMessage(nextState, message);
    const auto insights = _batteryTracker.Observe(nextState, now);
    nextState.estimatedMinutesRemaining = insights.minutesRemaining;
    nextState.asymmetricBatteryWarning = insights.asymmetricWarning;

    if (!changed && _cachedState == nextState) {
        return std::nullopt;
    }

    _lastBleTimestamp = now;
    auto oldState = std::move(_cachedState);
    _cachedState = std::move(nextState);
    return UpdateEvent{.oldState = std::move(oldState), .newState = _cachedState.value()};
}

auto StateManager::SetAapConnected(bool connected, Model fallbackModel)
    -> std::optional<UpdateEvent>
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_aapConnected == connected) {
        return std::nullopt;
    }
    _aapConnected = connected;

    if (!_cachedState.has_value()) {
        // Do not fabricate an empty-battery State before live battery/state frames arrive;
        // Settings enables live controls directly via `aapConnected + boundModel`, while the
        // popup waits for real device state so it never displays ANC controls without batteries.
        return std::nullopt;
    }

    State nextState = _cachedState.value();
    if (nextState.model == Model::Unknown && fallbackModel != Model::Unknown) {
        nextState.model = fallbackModel;
    }
    nextState.noiseControl.controllable =
        connected && CapabilitiesFor(nextState.model).SupportsNoiseControl();
    if (!connected) {
        nextState.noiseControl.activeMode.reset();
        nextState.noiseControl.conversationalAwareness.reset();
        nextState.noiseControl.conversationalAwarenessSpeaking.reset();
        nextState.noiseControl.conversationalAwarenessLevel = 0;
        nextState.noiseControl.oneBudAnc.reset();
        nextState.noiseControl.personalizedVolume.reset();
        nextState.noiseControl.loudSoundReduction.reset();
        nextState.noiseControl.headGestures.reset();
        nextState.noiseControl.adaptiveNoiseLevel.reset();
        nextState.noiseControl.microphoneSide.reset();
        nextState.noiseControl.toneVolume.reset();
        nextState.noiseControl.allowOffListeningMode.reset();
        nextState.noiseControl.listeningModeCycleMask.reset();
        nextState.noiseControl.pressSpeed.reset();
        nextState.noiseControl.pressHoldDuration.reset();
        nextState.noiseControl.hearingAidEnabled.reset();
        nextState.noiseControl.mediaAssistEnabled.reset();
        nextState.noiseControl.heartRateMonitoringEnabled.reset();
        nextState.noiseControl.heartRateStatus = HeartRateStatus::Off;
        nextState.noiseControl.heartRateBpm.reset();
        nextState.noiseControl.heartRateConfidence.reset();
        nextState.noiseControl.heartRateMinBpm.reset();
        nextState.noiseControl.heartRateMaxBpm.reset();
        nextState.noiseControl.heartRateAvgBpm.reset();
        nextState.noiseControl.heartRateHistory.clear();
    }
    if (nextState == _cachedState.value()) {
        return std::nullopt;
    }

    auto oldState = std::move(_cachedState);
    _cachedState = std::move(nextState);
    return UpdateEvent{.oldState = std::move(oldState), .newState = _cachedState.value()};
}

auto StateManager::SetAudioProfile(AudioProfile profile, QString sourceProcess)
    -> std::optional<UpdateEvent>
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_audioProfile == profile && _audioProfileSource == sourceProcess) {
        return std::nullopt;
    }
    _audioProfile = profile;
    _audioProfileSource = std::move(sourceProcess);

    if (!_cachedState.has_value() || (_cachedState->audioProfile == _audioProfile &&
                                      _cachedState->audioProfileSource == _audioProfileSource))
    {
        return std::nullopt;
    }

    auto oldState = _cachedState;
    _cachedState->audioProfile = _audioProfile;
    _cachedState->audioProfileSource = _audioProfileSource;
    return UpdateEvent{.oldState = std::move(oldState), .newState = _cachedState.value()};
}

auto StateManager::NextExpiry(Timestamp now) const -> std::optional<Clock::duration>
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (_aapConnected) {
        return std::nullopt;
    }

    std::optional<Timestamp> earliest;
    const auto consider = [&](const std::optional<Timestamp> &ts) {
        if (!ts.has_value()) {
            return;
        }
        const Timestamp deadline = ts.value() + kStateTimeout;
        if (!earliest.has_value() || deadline < earliest.value()) {
            earliest = deadline;
        }
    };

    consider(_lastBleTimestamp);
    if (_adv.left.has_value()) {
        consider(_adv.left->second);
    }
    if (_adv.right.has_value()) {
        consider(_adv.right->second);
    }

    if (!earliest.has_value()) {
        return std::nullopt;
    }
    if (earliest.value() <= now) {
        return Clock::duration::zero();
    }
    return earliest.value() - now;
}

bool StateManager::ExpireStale(Timestamp now)
{
    std::function<void()> onDiscardState;
    bool discarded = false;

    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_aapConnected) {
            return false;
        }

        if (_adv.left.has_value() && now - _adv.left->second >= kStateTimeout) {
            LOG(Info, "StateManager: Left side advertisement expired.");
            _adv.left.reset();
        }
        if (_adv.right.has_value() && now - _adv.right->second >= kStateTimeout) {
            LOG(Info, "StateManager: Right side advertisement expired.");
            _adv.right.reset();
        }

        if (_lastBleTimestamp.has_value() && now - _lastBleTimestamp.value() >= kStateTimeout) {
            if (_cachedState.has_value()) {
                LOG(Info, "StateManager: Device is lost.");
                discarded = true;
            }
            onDiscardState = ResetAll();
        }
    }

    if (onDiscardState) {
        onDiscardState();
    }
    return discarded;
}

void StateManager::Disconnect()
{
    std::function<void()> onDiscardState;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        LOG(Info, "StateManager: Disconnect.");
        _aapConnected = false;
        _audioProfile = AudioProfile::Unknown;
        _audioProfileSource.clear();
        _batteryTracker.Reset();
        onDiscardState = ResetAll();
    }
    if (onDiscardState) {
        onDiscardState();
    }
}

void StateManager::OnRssiMinChanged(int16_t rssiMin)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _rssiMin = rssiMin;
}

void StateManager::SetOnDiscardState(std::function<void()> callback)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _onDiscardState = std::move(callback);
}

bool StateManager::IsPossibleDesiredAdv(const Advertisement &adv) const
{
    const auto advRssi = adv.GetRssi();
    if (advRssi < _rssiMin) {
        LOG(Warn,
            "IsPossibleDesiredAdv returns false. Reason: RSSI is less than the limit. "
            "curr: '{}' min: '{}'",
            advRssi, _rssiMin);
        return false;
    }

    const auto &advState = adv.GetAdvState();
    auto &lastAdv = advState.side == Side::Left ? _adv.left : _adv.right;
    auto &lastAnotherAdv = advState.side == Side::Left ? _adv.right : _adv.left;

    const auto hasDifferentModel = [&](const auto &cachedAdv) {
        if (!cachedAdv.has_value()) {
            return false;
        }
        const auto cachedModel = cachedAdv->first.GetAdvState().model;
        return cachedModel != Model::Unknown && advState.model != Model::Unknown &&
               cachedModel != advState.model;
    };
    if (hasDifferentModel(lastAdv) || hasDifferentModel(lastAnotherAdv)) {
        LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: model does not match cached state");
        return false;
    }

    if (lastAdv.has_value() && lastAdv->first.GetAddress() != adv.GetAddress()) {
        const auto &lastAdvState = lastAdv->first.GetAdvState();

        Battery::ValueType leftBatteryDiff = 0, rightBatteryDiff = 0, caseBatteryDiff = 0;
        using SignedBatteryValue = std::make_signed_t<Battery::ValueType>;

        if (advState.pods.left.battery.Available() && lastAdvState.pods.left.battery.Available()) {
            leftBatteryDiff = std::abs(
                static_cast<SignedBatteryValue>(advState.pods.left.battery.Value()) -
                static_cast<SignedBatteryValue>(lastAdvState.pods.left.battery.Value()));
        }
        if (advState.pods.right.battery.Available() && lastAdvState.pods.right.battery.Available())
        {
            rightBatteryDiff = std::abs(
                static_cast<SignedBatteryValue>(advState.pods.right.battery.Value()) -
                static_cast<SignedBatteryValue>(lastAdvState.pods.right.battery.Value()));
        }
        if (advState.caseBox.battery.Available() && lastAdvState.caseBox.battery.Available()) {
            caseBatteryDiff = std::abs(
                static_cast<SignedBatteryValue>(advState.caseBox.battery.Value()) -
                static_cast<SignedBatteryValue>(lastAdvState.caseBox.battery.Value()));
        }

        if (leftBatteryDiff > kMaxAddressRotationBatteryDiff ||
            rightBatteryDiff > kMaxAddressRotationBatteryDiff ||
            caseBatteryDiff > kMaxAddressRotationBatteryDiff)
        {
            LOG(Warn,
                "IsPossibleDesiredAdv returns false. Reason: BatteryDiff l='{}' r='{}' c='{}'",
                leftBatteryDiff, rightBatteryDiff, caseBatteryDiff);
            return false;
        }

        const int16_t rssiDiff = std::abs(advRssi - lastAdv->first.GetRssi());
        if (rssiDiff > kMaxRssiDiff) {
            LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: Current side rssiDiff '{}'",
                rssiDiff);
            return false;
        }

        LOG(Warn, "Address changed, but it might still be the same device.");
    }

    if (lastAnotherAdv.has_value()) {
        const int16_t rssiDiff = std::abs(advRssi - lastAnotherAdv->first.GetRssi());
        if (rssiDiff > kMaxRssiDiff) {
            LOG(Warn, "IsPossibleDesiredAdv returns false. Reason: Another side rssiDiff '{}'",
                rssiDiff);
            return false;
        }
    }

    return true;
}

void StateManager::UpdateAdv(Advertisement adv, Timestamp now)
{
    _lastBleTimestamp = now;

    const auto &advState = adv.GetAdvState();
    if (advState.side == Side::Left) {
        _adv.left = std::make_pair(std::move(adv), now);
    }
    else if (advState.side == Side::Right) {
        _adv.right = std::make_pair(std::move(adv), now);
    }
}

auto StateManager::UpdateState(Timestamp now) -> std::optional<UpdateEvent>
{
    const auto pickSide = [&](auto predicate) -> const Advertisement::AdvState & {
        static const Advertisement::AdvState kEmpty{};
        const bool hasLeft = _adv.left.has_value();
        const bool hasRight = _adv.right.has_value();
        const bool leftMatches = hasLeft && predicate(_adv.left->first.GetAdvState());
        const bool rightMatches = hasRight && predicate(_adv.right->first.GetAdvState());

        if (leftMatches && rightMatches) {
            return _adv.left->second > _adv.right->second ? _adv.left->first.GetAdvState()
                                                          : _adv.right->first.GetAdvState();
        }
        if (leftMatches) {
            return _adv.left->first.GetAdvState();
        }
        if (rightMatches) {
            return _adv.right->first.GetAdvState();
        }

        // Neither side satisfies the availability predicate (for example, case battery is
        // unavailable while both buds are worn). Fall back to whichever cached side is
        // actually present and freshest so flags like `isLidOpened` are never lost from a
        // left-only advertisement.
        if (hasLeft && hasRight) {
            return _adv.left->second > _adv.right->second ? _adv.left->first.GetAdvState()
                                                          : _adv.right->first.GetAdvState();
        }
        if (hasLeft) {
            return _adv.left->first.GetAdvState();
        }
        if (hasRight) {
            return _adv.right->first.GetAdvState();
        }
        return kEmpty;
    };

    State newState;
    const auto &modelSource =
        pickSide([](const Advertisement::AdvState &s) { return s.model != Model::Unknown; });
    newState.model = modelSource.model;
    newState.colour = modelSource.colour;
    newState.pods.left = pickSide([](const Advertisement::AdvState &s) {
                             return s.pods.left.battery.Available();
                         }).pods.left;
    newState.pods.right = pickSide([](const Advertisement::AdvState &s) {
                              return s.pods.right.battery.Available();
                          }).pods.right;
    newState.caseBox = pickSide([](const Advertisement::AdvState &s) {
                           return s.caseBox.battery.Available();
                       }).caseBox;
    newState.audioProfile = _audioProfile;
    newState.audioProfileSource = _audioProfileSource;

    // Preserve AAP-supplied enrichment when BLE updates arrive alongside a live AAP link.
    if (_cachedState.has_value()) {
        newState.noiseControl = _cachedState->noiseControl;
        newState.firmwareVersion = _cachedState->firmwareVersion;
        newState.serialNumber = _cachedState->serialNumber;
        if (_aapConnected && _cachedState->batteryPrecision == BatteryPrecision::ExactPercent) {
            newState.batteryPrecision = BatteryPrecision::ExactPercent;
            newState.pods = _cachedState->pods;
            if (_cachedState->caseBox.battery.Available()) {
                newState.caseBox.battery = _cachedState->caseBox.battery;
                newState.caseBox.isCharging = _cachedState->caseBox.isCharging;
            }
        }
    }

    const auto insights = _batteryTracker.Observe(newState, now);
    newState.estimatedMinutesRemaining = insights.minutesRemaining;
    newState.asymmetricBatteryWarning = insights.asymmetricWarning;

    if (newState == _cachedState) {
        return std::nullopt;
    }

    auto oldState = std::move(_cachedState);
    _cachedState = std::move(newState);
    return UpdateEvent{.oldState = std::move(oldState), .newState = _cachedState.value()};
}

std::function<void()> StateManager::ResetAll()
{
    auto onDiscardState = _cachedState.has_value() ? _onDiscardState : nullptr;

    _adv.left.reset();
    _adv.right.reset();
    _lastBleTimestamp.reset();
    _cachedState.reset();
    return onDiscardState;
}

} // namespace Core::AirPods::Details
