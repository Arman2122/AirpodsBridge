// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include <QString>

#include "Source/Domain/AirPodsState.h"

namespace Core::AirPods::Aap {

//
// Apple Accessory Protocol (AAP) packet codec for BR/EDR L2CAP (PSM 0x1001, service UUID
// {74EC2172-0BAD-4D01-8F77-997B2BE0722A}).
//
// Pure domain logic with zero platform calls. A future Windows KMDF L2CAP profile driver
// (or test fake) hands raw received L2CAP frames to `DecodePacket` and sends the byte
// vectors produced by `Build*` straight over the socket/IOCTL.
//

constexpr uint16_t kDefaultL2capPsm = 0x1001;

enum class ComponentType : uint8_t {
    Single = 0x01,
    Right = 0x02,
    Left = 0x04,
    Case = 0x08,
};

enum class ChargeStatus : uint8_t {
    Charging = 0x01,
    NotCharging = 0x02,
    Disconnected = 0x04,
};

enum class EarPlacement : uint8_t {
    InEar = 0x00,
    OutOfEar = 0x01,
    InCase = 0x02,
    Disconnected = 0x03,
    InMotion = 0x04,
};

struct BatteryEntry {
    ComponentType component{ComponentType::Single};
    uint8_t level{0}; // 0..100 (1% granularity)
    ChargeStatus status{ChargeStatus::Disconnected};

    bool operator==(const BatteryEntry &) const = default;
};

struct BatteryNotification {
    std::vector<BatteryEntry> entries;

    bool operator==(const BatteryNotification &) const = default;
};

struct EarDetectionNotification {
    EarPlacement primary{EarPlacement::OutOfEar};
    EarPlacement secondary{EarPlacement::OutOfEar};

    bool operator==(const EarDetectionNotification &) const = default;
};

struct NoiseControlNotification {
    NoiseControlMode mode{NoiseControlMode::Off};

    bool operator==(const NoiseControlNotification &) const = default;
};

struct ConversationalAwarenessNotification {
    bool enabled{false};

    bool operator==(const ConversationalAwarenessNotification &) const = default;
};

enum class ToggleFeature : uint8_t {
    OneBudAnc = 0x1B,
    PersonalizedVolume = 0x26,
    LoudSoundReduction = 0x29,
    HeartRateMonitoring = 0x30,
    HeadGestures = 0x31,
    AllowOffListeningMode = 0x34,
    HearingAid = 0x2C,
    MediaAssist = 0x2D,
};

struct FeatureToggleNotification {
    ToggleFeature feature{ToggleFeature::OneBudAnc};
    bool enabled{false};

    bool operator==(const FeatureToggleNotification &) const = default;
};

struct AdaptiveNoiseLevelNotification {
    uint8_t level{50}; // 0..100

    bool operator==(const AdaptiveNoiseLevelNotification &) const = default;
};

struct MicrophoneSideNotification {
    MicrophoneSide side{MicrophoneSide::Auto};

    bool operator==(const MicrophoneSideNotification &) const = default;
};

struct ToneVolumeNotification {
    uint8_t volume{100}; // 0..100

    bool operator==(const ToneVolumeNotification &) const = default;
};

struct ListeningModeCycleNotification {
    uint8_t modeMask{0}; // Bitmask of ListeningCycleBit

    bool operator==(const ListeningModeCycleNotification &) const = default;
};

struct PressSpeedNotification {
    PressSpeed speed{PressSpeed::Default};

    bool operator==(const PressSpeedNotification &) const = default;
};

struct PressHoldDurationNotification {
    PressHoldDuration duration{PressHoldDuration::Default};

    bool operator==(const PressHoldDurationNotification &) const = default;
};

struct ConversationalAwarenessSpeechNotification {
    bool speaking{false};
    // Raw AAP 0x4B byte 9 level: 1..2 = speaking (lowest media volume), 3..7 = gradual recovery,
    // 8..9 = stopped speaking / normal volume.
    uint8_t level{0};

    bool operator==(const ConversationalAwarenessSpeechNotification &) const = default;
};

struct HeartRateNotification {
    uint16_t bpm{0};
    uint8_t confidence{0};

    bool operator==(const HeartRateNotification &) const = default;
};

struct HeartRateStatusNotification {
    HeartRateStatus status{HeartRateStatus::Off};

    bool operator==(const HeartRateStatusNotification &) const = default;
};

struct MetadataNotification {
    QString deviceName;
    QString modelNumber;
    QString manufacturer;
    QString serialNumber;
    QString firmwareVersion;

    bool operator==(const MetadataNotification &) const = default;
};

using InboundMessage = std::variant<
    BatteryNotification, EarDetectionNotification, NoiseControlNotification,
    ConversationalAwarenessNotification, ConversationalAwarenessSpeechNotification,
    FeatureToggleNotification, AdaptiveNoiseLevelNotification, MicrophoneSideNotification,
    ToneVolumeNotification, ListeningModeCycleNotification, PressSpeedNotification,
    PressHoldDurationNotification, HeartRateNotification, HeartRateStatusNotification,
    MetadataNotification>;

// Outbound control & session-setup packets.
std::vector<uint8_t> BuildHandshakePacket();
std::vector<uint8_t> BuildRequestNotificationsPacket();
std::vector<uint8_t> BuildEnableFeaturesPacket();
std::vector<uint8_t> BuildSetNoiseControlPacket(NoiseControlMode mode);
std::vector<uint8_t> BuildSetConversationalAwarenessPacket(bool enabled);
std::vector<uint8_t> BuildSetOneBudAncPacket(bool enabled);
std::vector<uint8_t> BuildSetPersonalizedVolumePacket(bool enabled);
std::vector<uint8_t> BuildSetLoudSoundReductionPacket(bool enabled);
std::vector<uint8_t> BuildSetAdaptiveNoiseLevelPacket(uint8_t level);
std::vector<uint8_t> BuildSetHeadGesturesPacket(bool enabled);
std::vector<uint8_t> BuildSetMicrophoneSidePacket(MicrophoneSide side);
std::vector<uint8_t> BuildSetToneVolumePacket(uint8_t volumePercent);
std::vector<uint8_t> BuildSetAllowOffListeningModePacket(bool allow);
std::vector<uint8_t> BuildSetListeningModeCyclePacket(uint8_t modeMask);
std::vector<uint8_t> BuildSetPressSpeedPacket(PressSpeed speed);
std::vector<uint8_t> BuildSetPressHoldDurationPacket(PressHoldDuration duration);
std::vector<uint8_t> BuildSetHearingAidPacket(bool enabled);
std::vector<uint8_t> BuildSetMediaAssistPacket(bool enabled);
std::vector<uint8_t> BuildSetHeartRateMonitoringPacket(bool enabled);
std::vector<uint8_t> BuildRequestHeartRatePacket();
std::vector<uint8_t> BuildStartSidebandMicPacket();
std::vector<uint8_t> BuildStopSidebandMicPacket();
std::vector<uint8_t> BuildSetRenamePacket(const std::string &utf8Name);

// Bounds-checked decoder for inbound AAP frames. Returns `std::nullopt` for unknown,
// malformed, or truncated packets.
std::optional<InboundMessage> DecodePacket(std::span<const uint8_t> packet);

// Merges a decoded AAP message into `state`. Returns true if any observable field changed.
// `primaryIsLeft` identifies which physical bud maps to `EarDetectionNotification::primary`
// (true by default when the primary bud is left or unknown).
bool ApplyInboundMessage(State &state, const InboundMessage &message, bool primaryIsLeft = true);

} // namespace Core::AirPods::Aap
