// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <optional>
#include <vector>

#include <QString>

#include "Source/Domain/Base.h"

namespace Core::AirPods {

//
// The observable state of a device, reconstructed from its BLE advertisements and, when
// an AAP control transport is connected, enriched with exact battery, noise-control, and
// metadata updates.
//

namespace Details {

struct BasicState {
    Battery battery;
    bool isCharging{false};

    bool operator==(const BasicState &rhs) const = default;
};
} // namespace Details

struct PodState : Details::BasicState {
    bool isInEar{false};

    bool operator==(const PodState &rhs) const = default;
};

struct CaseState : Details::BasicState {
    bool isBothPodsInCase{false};
    bool isLidOpened{false};

    bool operator==(const CaseState &rhs) const = default;
};

struct PodsState {
    PodState left, right;

    bool operator==(const PodsState &rhs) const = default;
};

struct NoiseControlState {
    std::optional<NoiseControlMode> activeMode;
    // True only when a two-way control transport (AAP L2CAP) is connected and accepting
    // mode changes. Presentation uses this so BLE-only sessions never show dead controls.
    bool controllable{false};
    std::optional<bool> conversationalAwareness;
    std::optional<bool> conversationalAwarenessSpeaking;
    // Raw AAP 0x4B Conversation Awareness ducking level (0 = idle, 1..2 = speaking/ducked,
    // 3..7 = gradual volume recovery, 8..9 = normal).
    uint8_t conversationalAwarenessLevel{0};
    std::optional<bool> oneBudAnc;
    std::optional<bool> personalizedVolume;
    std::optional<bool> loudSoundReduction;
    std::optional<bool> headGestures;
    std::optional<uint8_t> adaptiveNoiseLevel;
    std::optional<MicrophoneSide> microphoneSide;
    std::optional<uint8_t> toneVolume;
    std::optional<bool> allowOffListeningMode;
    std::optional<uint8_t> listeningModeCycleMask;
    std::optional<PressSpeed> pressSpeed;
    std::optional<PressHoldDuration> pressHoldDuration;
    std::optional<bool> hearingAidEnabled;
    std::optional<bool> mediaAssistEnabled;
    std::optional<bool> heartRateMonitoringEnabled;
    HeartRateStatus heartRateStatus{HeartRateStatus::Off};
    std::optional<uint16_t> heartRateBpm;
    std::optional<uint8_t>
        heartRateConfidence; // 0..255 raw optical lock confidence (>= 0x80 locked)
    std::optional<uint16_t> heartRateMinBpm;
    std::optional<uint16_t> heartRateMaxBpm;
    std::optional<uint16_t> heartRateAvgBpm;
    std::vector<uint16_t> heartRateHistory;

    bool operator==(const NoiseControlState &rhs) const = default;
};

struct State {
    Model model{Model::Unknown};
    PodsState pods;
    CaseState caseBox;
    QString displayName;
    // The colour code the device broadcasts, Apple's own. The popup's artwork is picked
    // by it.
    uint8_t colour{0};
    BatteryPrecision batteryPrecision{BatteryPrecision::CoarseTenPercent};
    NoiseControlState noiseControl;
    AudioProfile audioProfile{AudioProfile::Unknown};
    // Process holding the HFP audio session (e.g. "Discord.exe"), when known.
    QString audioProfileSource;
    QString firmwareVersion;
    QString serialNumber;
    // Observed discharging estimate (minutes) from `BatteryTracker`, when enough drops
    // have been measured in the current session.
    std::optional<int> estimatedMinutesRemaining;
    bool asymmetricBatteryWarning{false};
    // True when reconstructed from a BLE proximity-pairing broadcast while the AirPods are
    // not connected to this Windows PC (for example, currently connected to an iPhone/iPad).
    bool isNearbyOtherDevice{false};

    bool operator==(const State &rhs) const = default;
};

//
// The one battery of a one-piece device (`IsOnePiece`), read out of the pair's layout.
//
// Which side carries it is the device's business, and a device may mirror it on both. So
// this is whichever side reports a level; when both do, the lower, and charging if either
// side says so.
//
inline Details::BasicState OnePieceState(const PodsState &pods)
{
    const auto &left = pods.left, &right = pods.right;
    if (!left.battery.Available()) {
        return right;
    }
    if (!right.battery.Available()) {
        return left;
    }
    Details::BasicState result = left.battery.Value() <= right.battery.Value() ? left : right;
    result.isCharging = left.isCharging || right.isCharging;
    return result;
}

//
// Whether a one-piece device (`IsOnePiece`) reports being worn. Either side may carry the
// flag in its advertisement.
//
inline bool OnePieceInEar(const PodsState &pods)
{
    return pods.left.isInEar || pods.right.isInEar;
}

namespace Details {

QString ResolveDisplayName(QString deviceName, Model model);
} // namespace Details

} // namespace Core::AirPods
