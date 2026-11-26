// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Presentation/MainWindowPresentation.h"

#include "Source/Domain/BatteryTracker.h"

namespace Gui {
namespace {

BatteryPresentation PresentBattery(const Core::AirPods::Details::BasicState &state)
{
    if (!state.battery.Available()) {
        return {};
    }

    return {
        .visible = true,
        .charging = state.isCharging,
        .value = state.battery.Value(),
    };
}

} // namespace

void MainWindowViewModel::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _connecting = false;
    _transientStatus.clear();
    _state = state;
    _nearbyState.reset();
}

void MainWindowViewModel::UpdateNearbyState(const Core::AirPods::State &state)
{
    if (_status == Status::Updating && _state.has_value()) {
        return;
    }
    _nearbyState = state;
}

void MainWindowViewModel::ClearNearby()
{
    _nearbyState.reset();
}

void MainWindowViewModel::SetBoundDevice(Core::AirPods::Model model, const QString &displayName)
{
    _boundModel = model;
    _boundDisplayName = displayName;
}

void MainWindowViewModel::Available()
{
    if (_status == Status::Unavailable) {
        _status = Status::Disconnected;
    }
}

void MainWindowViewModel::Unavailable()
{
    _status = Status::Unavailable;
    _connecting = false;
    _state.reset();
    _nearbyState.reset();
}

void MainWindowViewModel::Disconnect()
{
    if (_status == Status::Unbind) {
        return;
    }

    _status = Status::Disconnected;
    _connecting = false;
    _state.reset();
}

void MainWindowViewModel::Bind()
{
    _status = Status::Disconnected;
    _connecting = false;
    _state.reset();
}

void MainWindowViewModel::Unbind()
{
    _status = Status::Unbind;
    _connecting = false;
    _boundModel = Core::AirPods::Model::Unknown;
    _boundDisplayName.clear();
    _state.reset();
    _nearbyState.reset();
}

void MainWindowViewModel::SetQuickConnectAvailable(bool available)
{
    _quickConnectAvailable = available;
}

void MainWindowViewModel::SetShowHeartRateInPopup(bool show)
{
    _showHeartRateInPopup = show;
}

void MainWindowViewModel::SetConnecting(bool connecting)
{
    _connecting = connecting;
    if (connecting) {
        _transientStatus.clear();
    }
}

void MainWindowViewModel::SetTransientStatus(const QString &status)
{
    _transientStatus = status;
}

MainWindowPresentation MainWindowViewModel::Present() const
{
    MainWindowPresentation result;

    switch (_status) {
    case Status::Unavailable:
    case Status::Unbind:
        result.title = DisplayableStatus(_status);
        break;
    case Status::Disconnected:
        if (_boundModel != Core::AirPods::Model::Unknown) {
            result.title =
                _boundDisplayName.isEmpty() ? Helper::ToString(_boundModel) : _boundDisplayName;
            result.statusBadge = DisplayableStatus(_status);
            result.artwork = DeviceArtwork::Find(_boundModel);
        }
        else {
            result.title = DisplayableStatus(_status);
        }
        break;
    default:
        break;
    }

    if (_status == Status::Unbind) {
        result.buttonAction = ButtonAction::Bind;
    }
    else if (_status == Status::Disconnected && _quickConnectAvailable) {
        result.buttonAction = ButtonAction::Connect;
        result.connecting = _connecting;
    }

    if (!_state.has_value()) {
        if (!_nearbyState.has_value()) {
            if (!_transientStatus.isEmpty()) {
                result.statusBadge = _transientStatus;
            }
            return result;
        }

        const auto &nearby = _nearbyState.value();
        result.isNearby = true;
        result.title =
            nearby.displayName.isEmpty() ? Helper::ToString(nearby.model) : nearby.displayName;
        result.artwork = DeviceArtwork::Find(nearby.model, nearby.colour);
        if (!_transientStatus.isEmpty()) {
            result.statusBadge = _transientStatus;
        }
        else if (_connecting) {
            result.statusBadge = QStringLiteral("Connecting\u2026");
        }
        else if (nearby.caseBox.isLidOpened) {
            result.statusBadge = QStringLiteral("Nearby \u00B7 Case open");
        }
        else if (nearby.caseBox.isBothPodsInCase) {
            result.statusBadge = QStringLiteral("In case \u00B7 Nearby");
        }
        else {
            result.statusBadge = QStringLiteral("Connected to another device");
        }
        if (_status == Status::Unbind) {
            result.buttonAction = ButtonAction::Bind;
        }
        else if (_quickConnectAvailable) {
            result.buttonAction = ButtonAction::Connect;
            result.connecting = _connecting;
        }

        const auto caps = Core::AirPods::CapabilitiesFor(nearby.model);
        if (caps.isOnePiece) {
            result.leftBattery = PresentBattery(Core::AirPods::OnePieceState(nearby.pods));
            return result;
        }
        result.leftBattery = PresentBattery(nearby.pods.left);
        result.rightBattery = PresentBattery(nearby.pods.right);
        result.caseBattery = PresentBattery(nearby.caseBox);
        return result;
    }

    result.title = _state->displayName;
    result.artwork = DeviceArtwork::Find(_state->model, _state->colour);

    if (_state->audioProfile == Core::AirPods::AudioProfile::HandsFree) {
        result.statusBadge =
            _state->audioProfileSource.isEmpty()
                ? QStringLiteral("Hands-Free Audio")
                : QStringLiteral("Hands-Free (%1)").arg(_state->audioProfileSource);
    }
    else if (_state->asymmetricBatteryWarning) {
        result.statusBadge = QStringLiteral("Uneven charge \u2014 check case pins");
    }
    else if (_state->estimatedMinutesRemaining.has_value()) {
        result.statusBadge =
            QStringLiteral("%1 left").arg(Core::AirPods::BatteryTracker::FormatDuration(
                _state->estimatedMinutesRemaining.value()));
    }

    const auto caps = Core::AirPods::CapabilitiesFor(_state->model);
    if (caps.isOnePiece) {
        result.leftBattery = PresentBattery(Core::AirPods::OnePieceState(_state->pods));
    }
    else {
        result.leftBattery = PresentBattery(_state->pods.left);
        result.rightBattery = PresentBattery(_state->pods.right);
        result.caseBattery = PresentBattery(_state->caseBox);
    }

    const bool hasAnyBattery =
        result.leftBattery.visible || result.rightBattery.visible || result.caseBattery.visible;
    const bool hasLiveReading = hasAnyBattery || _state->noiseControl.activeMode.has_value();
    if (!hasAnyBattery && result.statusBadge.isEmpty()) {
        result.statusBadge = _state->noiseControl.controllable ? QStringLiteral("Connected")
                                                               : QStringLiteral("Connecting\u2026");
    }
    const bool liveConnectedAap = _status == Status::Updating && !_state->isNearbyOtherDevice &&
                                  _state->noiseControl.controllable && hasLiveReading;

    if (liveConnectedAap && caps.SupportsNoiseControl()) {
        result.noiseControl.visible = true;
        result.noiseControl.activeMode =
            _state->noiseControl.activeMode.value_or(Core::AirPods::NoiseControlMode::Off);
        result.noiseControl.supportsAdaptive = caps.supportsAdaptiveAudio;
        result.noiseControl.adaptiveNoiseLevel =
            _state->noiseControl.adaptiveNoiseLevel.value_or(50);
        result.noiseControl.allowOffListeningMode =
            _state->noiseControl.allowOffListeningMode.value_or(false);
        result.noiseControl.supportsConversationalAwareness = caps.supportsConversationalAwareness;
        result.noiseControl.conversationalAwareness =
            _state->noiseControl.conversationalAwareness.value_or(false);
        result.noiseControl.conversationalAwarenessSpeaking =
            result.noiseControl.conversationalAwareness &&
            _state->noiseControl.conversationalAwarenessSpeaking.value_or(false);
        result.noiseControl.supportsPersonalizedVolume = caps.supportsAdaptiveAudio;
        result.noiseControl.personalizedVolume =
            _state->noiseControl.personalizedVolume.value_or(false);
        result.noiseControl.supportsHearingAid = caps.supportsHearingAid;
        result.noiseControl.hearingAidEnabled =
            _state->noiseControl.hearingAidEnabled.value_or(false);
        result.noiseControl.heartRateBpm = _state->noiseControl.heartRateBpm;
    }

    if (liveConnectedAap && _showHeartRateInPopup && caps.supportsHeartRate) {
        result.heartRate.visible = true;
        result.heartRate.sensorEnabled =
            _state->noiseControl.heartRateMonitoringEnabled.value_or(false);
        result.heartRate.status = _state->noiseControl.heartRateStatus;
        result.heartRate.bpm = _state->noiseControl.heartRateBpm;
        result.heartRate.confidence = _state->noiseControl.heartRateConfidence;
        result.heartRate.history = _state->noiseControl.heartRateHistory;
    }

    // Only surface a compact BPM tag in the top status badge when the dedicated Heart Rate
    // bar is hidden, so the BPM is never repeated twice in the popup.
    if (!result.heartRate.visible) {
        if (_state->noiseControl.heartRateBpm.has_value()) {
            const QString bpmText = QStringLiteral("%1 %2 BPM")
                                        .arg(QChar{0x2665})
                                        .arg(_state->noiseControl.heartRateBpm.value());
            result.statusBadge =
                result.statusBadge.isEmpty()
                    ? bpmText
                    : QStringLiteral("%1 %2 %3").arg(bpmText, QChar{0x00B7}, result.statusBadge);
        }
        else if (
            _state->noiseControl.heartRateStatus == Core::AirPods::HeartRateStatus::AcquiringLock)
        {
            const QString lockText =
                QStringLiteral("%1 Measuring%2").arg(QChar{0x2665}).arg(QChar{0x2026});
            result.statusBadge =
                result.statusBadge.isEmpty()
                    ? lockText
                    : QStringLiteral("%1 %2 %3").arg(lockText, QChar{0x00B7}, result.statusBadge);
        }
    }

    return result;
}

} // namespace Gui
