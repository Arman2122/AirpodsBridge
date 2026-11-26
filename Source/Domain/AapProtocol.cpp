// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/AapProtocol.h"

#include <algorithm>
#include <array>
#include <numeric>

#include "Source/Domain/RtBuddySensor.h"

namespace Core::AirPods::Aap {

namespace {

// Standard AAP header prefix for data/control packets: [0x04, 0x00, 0x04, 0x00, opcode, 0x00].
constexpr std::array<uint8_t, 4> kDataHeader{0x04, 0x00, 0x04, 0x00};

constexpr uint8_t kOpcodeBattery = 0x04;
constexpr uint8_t kOpcodeEarDetection = 0x06;
constexpr uint8_t kOpcodeControlCommand = 0x09;
constexpr uint8_t kOpcodeRequestNotifications = 0x0F;
constexpr uint8_t kOpcodeRtBuddySensor = 0x17;
constexpr uint8_t kOpcodeRename = 0x1A;
constexpr uint8_t kOpcodeMetadata = 0x1D;
constexpr uint8_t kOpcodeConversationalSpeech = 0x4B;
constexpr uint8_t kOpcodeEnableFeatures = 0x4D;
constexpr uint8_t kOpcodeAudioStream = 0x58;
constexpr uint8_t kOpcodeHeartRate = 0x62;

constexpr uint8_t kSettingMicrophoneSide = 0x01;
constexpr uint8_t kSettingNoiseControl = 0x0D;
constexpr uint8_t kSettingPressSpeed = 0x17;
constexpr uint8_t kSettingPressHoldDuration = 0x18;
constexpr uint8_t kSettingListeningCycle = 0x1A;
constexpr uint8_t kSettingOneBudAnc = 0x1B;
constexpr uint8_t kSettingToneVolume = 0x1F;
constexpr uint8_t kSettingPersonalizedVolume = 0x26;
constexpr uint8_t kSettingConversationalAwareness = 0x28;
constexpr uint8_t kSettingLoudSoundReduction = 0x29;
constexpr uint8_t kSettingHearingAid = 0x2C;
constexpr uint8_t kSettingMediaAssist = 0x2D;
constexpr uint8_t kSettingAdaptiveNoiseLevel = 0x2E;
constexpr uint8_t kSettingHeartRateMonitoring = 0x30;
constexpr uint8_t kSettingHeadGestures = 0x31;
constexpr uint8_t kSettingAllowOffMode = 0x34;

constexpr size_t kMaxRenameBytes = 240;

bool HasDataHeader(std::span<const uint8_t> packet)
{
    return packet.size() >= 6 &&
           std::equal(kDataHeader.begin(), kDataHeader.end(), packet.begin()) && packet[5] == 0x00;
}

uint8_t ToWireNoiseControl(NoiseControlMode mode)
{
    switch (mode) {
    case NoiseControlMode::Off:
        return 0x01;
    case NoiseControlMode::NoiseCancellation:
        return 0x02;
    case NoiseControlMode::Transparency:
        return 0x03;
    case NoiseControlMode::Adaptive:
        return 0x04;
    default:
        return 0x01;
    }
}

std::optional<NoiseControlMode> FromWireNoiseControl(uint8_t value)
{
    switch (value) {
    case 0x01:
        return NoiseControlMode::Off;
    case 0x02:
        return NoiseControlMode::NoiseCancellation;
    case 0x03:
        return NoiseControlMode::Transparency;
    case 0x04:
        return NoiseControlMode::Adaptive;
    default:
        return std::nullopt;
    }
}

std::optional<ComponentType> FromWireComponent(uint8_t value)
{
    switch (value) {
    case static_cast<uint8_t>(ComponentType::Single):
        return ComponentType::Single;
    case static_cast<uint8_t>(ComponentType::Right):
        return ComponentType::Right;
    case static_cast<uint8_t>(ComponentType::Left):
        return ComponentType::Left;
    case static_cast<uint8_t>(ComponentType::Case):
        return ComponentType::Case;
    default:
        return std::nullopt;
    }
}

std::optional<ChargeStatus> FromWireChargeStatus(uint8_t value)
{
    switch (value) {
    case static_cast<uint8_t>(ChargeStatus::Charging):
    case 0x05: // AirPods Pro 2/3 firmware may report 0x05 when trickle-charging in case.
        return ChargeStatus::Charging;
    case 0x00: // Some AirPods Pro 2/3 firmware uses 0x00 for not-charging/idle.
    case static_cast<uint8_t>(ChargeStatus::NotCharging):
        return ChargeStatus::NotCharging;
    case static_cast<uint8_t>(ChargeStatus::Disconnected):
        return ChargeStatus::Disconnected;
    default:
        return std::nullopt;
    }
}

std::optional<EarPlacement> FromWirePlacement(uint8_t value)
{
    switch (value) {
    case static_cast<uint8_t>(EarPlacement::InEar):
        return EarPlacement::InEar;
    case static_cast<uint8_t>(EarPlacement::OutOfEar):
        return EarPlacement::OutOfEar;
    case static_cast<uint8_t>(EarPlacement::InCase):
        return EarPlacement::InCase;
    case static_cast<uint8_t>(EarPlacement::Disconnected):
        return EarPlacement::Disconnected;
    case static_cast<uint8_t>(EarPlacement::InMotion):
        return EarPlacement::InMotion;
    default:
        return std::nullopt;
    }
}

std::optional<InboundMessage> DecodeBattery(std::span<const uint8_t> packet)
{
    // Format: [header(6), count(1), entries(count * 5)]
    // Entry:  [component, 0x01, level(0..100), status, 0x01]
    if (packet.size() < 7) {
        return std::nullopt;
    }

    const size_t count = packet[6];
    if (count == 0 || count > 8 || packet.size() < 7 + count * 5) {
        return std::nullopt;
    }

    BatteryNotification notification;
    notification.entries.reserve(count);

    for (size_t i = 0; i < count; ++i) {
        const size_t base = 7 + i * 5;
        const auto component = FromWireComponent(packet[base]);
        const auto status = FromWireChargeStatus(packet[base + 3]);
        if (!component.has_value() || !status.has_value()) {
            continue;
        }

        const uint8_t level = packet[base + 2];
        ChargeStatus resolvedStatus = status.value();
        if (level == 0x7F || level == 0xFF) {
            resolvedStatus = ChargeStatus::Disconnected;
        }
        if (resolvedStatus != ChargeStatus::Disconnected && level > 100) {
            continue;
        }

        notification.entries.push_back(BatteryEntry{
            .component = component.value(),
            .level = level,
            .status = resolvedStatus,
        });
    }

    if (notification.entries.empty()) {
        return std::nullopt;
    }
    return notification;
}

std::optional<InboundMessage> DecodeEarDetection(std::span<const uint8_t> packet)
{
    // Format: [header(6), primaryStatus, secondaryStatus]
    if (packet.size() < 8) {
        return std::nullopt;
    }

    const auto primary = FromWirePlacement(packet[6]);
    const auto secondary = FromWirePlacement(packet[7]);
    if (!primary.has_value() || !secondary.has_value()) {
        return std::nullopt;
    }

    return EarDetectionNotification{
        .primary = primary.value(),
        .secondary = secondary.value(),
    };
}

std::optional<InboundMessage> DecodeControlCommand(std::span<const uint8_t> packet)
{
    // Format: [header(6), settingId, value, ...]
    if (packet.size() < 8) {
        return std::nullopt;
    }

    const uint8_t settingId = packet[6];
    const uint8_t value = packet[7];

    if (settingId == kSettingNoiseControl) {
        const auto mode = FromWireNoiseControl(value);
        if (!mode.has_value()) {
            return std::nullopt;
        }
        return NoiseControlNotification{.mode = mode.value()};
    }

    if (settingId == kSettingConversationalAwareness) {
        if (value != 0x00 && value != 0x01 && value != 0x02) {
            return std::nullopt;
        }
        return ConversationalAwarenessNotification{.enabled = value == 0x01};
    }

    if (settingId == kSettingOneBudAnc || settingId == kSettingPersonalizedVolume ||
        settingId == kSettingLoudSoundReduction || settingId == kSettingHeadGestures ||
        settingId == kSettingAllowOffMode || settingId == kSettingHearingAid ||
        settingId == kSettingMediaAssist || settingId == kSettingHeartRateMonitoring)
    {
        if (value != 0x00 && value != 0x01 && value != 0x02) {
            return std::nullopt;
        }
        return FeatureToggleNotification{
            .feature = static_cast<ToggleFeature>(settingId),
            .enabled = value == 0x01,
        };
    }

    if (settingId == kSettingAdaptiveNoiseLevel) {
        if (value > 100) {
            return std::nullopt;
        }
        return AdaptiveNoiseLevelNotification{.level = value};
    }

    if (settingId == kSettingMicrophoneSide) {
        if (value == 0x01) {
            return MicrophoneSideNotification{.side = MicrophoneSide::Auto};
        }
        if (value == 0x02) {
            return MicrophoneSideNotification{.side = MicrophoneSide::AlwaysRight};
        }
        if (value == 0x03) {
            return MicrophoneSideNotification{.side = MicrophoneSide::AlwaysLeft};
        }
        return std::nullopt;
    }

    if (settingId == kSettingToneVolume) {
        if (value > 100) {
            return std::nullopt;
        }
        return ToneVolumeNotification{.volume = value};
    }

    if (settingId == kSettingListeningCycle) {
        return ListeningModeCycleNotification{.modeMask = static_cast<uint8_t>(value & 0x0Fu)};
    }

    if (settingId == kSettingPressSpeed) {
        if (value > 0x02) {
            return std::nullopt;
        }
        return PressSpeedNotification{.speed = static_cast<PressSpeed>(value)};
    }

    if (settingId == kSettingPressHoldDuration) {
        if (value > 0x02) {
            return std::nullopt;
        }
        return PressHoldDurationNotification{.duration = static_cast<PressHoldDuration>(value)};
    }

    return std::nullopt;
}

std::optional<InboundMessage> DecodeConversationalSpeech(std::span<const uint8_t> packet)
{
    // Real Apple AAP 0x4B format: [04 00 04 00 4B 00 02 00 01 <level>]
    // Byte 8 is always 0x01; byte 9 carries the Conversation Awareness ducking level:
    //   1..2 = started speaking (lowest media volume)
    //   3..7 = gradual recovery back toward normal volume
    //   8..9 = stopped speaking / normal volume
    if (packet.size() < 10) {
        return std::nullopt;
    }
    const uint8_t level = packet[9];
    if (level < 1 || level > 9) {
        return std::nullopt;
    }
    return ConversationalAwarenessSpeechNotification{
        .speaking = level <= 2,
        .level = level,
    };
}

std::optional<InboundMessage> DecodeHeartRate(std::span<const uint8_t> packet)
{
    // Legacy/compact 0x62 format: [header(6), flags(1), bpmLo(1), (optional bpmHi(1)), ...]
    if (packet.size() < 8) {
        return std::nullopt;
    }

    uint16_t bpm = packet[7];
    if (packet.size() >= 9) {
        const uint16_t bpm16 = static_cast<uint16_t>(packet[7]) |
                               static_cast<uint16_t>(static_cast<uint16_t>(packet[8]) << 8);
        if (bpm16 >= 30 && bpm16 <= 240) {
            bpm = bpm16;
        }
        else if (packet[8] != 0 && (bpm < 30 || bpm > 240)) {
            return std::nullopt;
        }
        else if (packet[8] != 0 && packet[8] <= 0x03) {
            // High byte looks like an out-of-range 16-bit BPM (e.g. 0x0100 = 256), not flags.
            return std::nullopt;
        }
    }
    if (bpm < 30 || bpm > 240) {
        return std::nullopt;
    }
    return HeartRateNotification{.bpm = bpm, .confidence = 0xFF};
}

std::optional<InboundMessage> DecodeMetadata(std::span<const uint8_t> packet)
{
    // Real Apple AAP 0x1D metadata carries NUL-terminated UTF-8 strings starting at byte 11
    // ([dataHeader(6), metaHeader(5), name\0, model\0, manufacturer\0, serial\0, fw\0...]).
    // If byte 11 is an extra leading NUL padding byte (synthetic 6-byte metaHeader), advance
    // once to byte 12 so both layouts decode cleanly without ever clipping the first letter.
    constexpr size_t kMinStringsOffset = 11;
    if (packet.size() <= kMinStringsOffset) {
        return std::nullopt;
    }

    size_t pos = kMinStringsOffset;
    if (packet[pos] == 0x00 && pos + 1 < packet.size()) {
        ++pos;
    }

    std::vector<QString> fields;
    while (pos < packet.size() && fields.size() < 8) {
        const auto endIt =
            std::find(packet.begin() + static_cast<std::ptrdiff_t>(pos), packet.end(), uint8_t{0});
        const size_t end = static_cast<size_t>(std::distance(packet.begin(), endIt));
        const size_t len = end - pos;
        fields.push_back(
            QString::fromUtf8(
                reinterpret_cast<const char *>(packet.data() + pos), static_cast<int>(len))
                .trimmed());
        if (end >= packet.size()) {
            break;
        }
        pos = end + 1;
    }

    if (fields.empty() ||
        std::all_of(fields.begin(), fields.end(), [](const QString &s) { return s.isEmpty(); }))
    {
        return std::nullopt;
    }

    MetadataNotification meta;
    if (fields.size() > 0) {
        meta.deviceName = fields[0];
    }
    if (fields.size() > 1) {
        meta.modelNumber = fields[1];
    }
    if (fields.size() > 2) {
        meta.manufacturer = fields[2];
    }
    if (fields.size() > 3) {
        meta.serialNumber = fields[3];
    }
    if (fields.size() > 4) {
        meta.firmwareVersion = fields[4];
    }
    return meta;
}

void ApplyBatteryPart(
    Details::BasicState &target, const BatteryEntry &entry, bool preserveOnDisconnect = false)
{
    if (entry.status == ChargeStatus::Disconnected) {
        if (!preserveOnDisconnect) {
            target.battery = {};
        }
        target.isCharging = false;
        return;
    }
    target.battery = Battery{entry.level};
    target.isCharging = entry.status == ChargeStatus::Charging;
}

} // namespace

std::vector<uint8_t> BuildHandshakePacket()
{
    return {
        0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
}

std::vector<uint8_t> BuildRequestNotificationsPacket()
{
    return {
        0x04, 0x00, 0x04, 0x00, kOpcodeRequestNotifications, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    };
}

std::vector<uint8_t> BuildEnableFeaturesPacket()
{
    // Enables H2 extended features (Adaptive Audio, Conversational Awareness, metadata).
    return {
        0x04, 0x00, 0x04, 0x00, kOpcodeEnableFeatures, 0x00, 0xFF, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    };
}

std::vector<uint8_t> BuildSetNoiseControlPacket(NoiseControlMode mode)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingNoiseControl,
        ToWireNoiseControl(mode),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetConversationalAwarenessPacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingConversationalAwareness,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetOneBudAncPacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingOneBudAnc,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetPersonalizedVolumePacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingPersonalizedVolume,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetLoudSoundReductionPacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingLoudSoundReduction,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetAdaptiveNoiseLevelPacket(uint8_t level)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingAdaptiveNoiseLevel,
        static_cast<uint8_t>(std::min<uint8_t>(level, 100)),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetHeadGesturesPacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingHeadGestures,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetMicrophoneSidePacket(MicrophoneSide side)
{
    uint8_t wire = 0x01;
    if (side == MicrophoneSide::AlwaysRight) {
        wire = 0x02;
    }
    else if (side == MicrophoneSide::AlwaysLeft) {
        wire = 0x03;
    }
    return {
        0x04, 0x00, 0x04, 0x00, kOpcodeControlCommand, 0x00, kSettingMicrophoneSide,
        wire, 0x00, 0x00, 0x00,
    };
}

std::vector<uint8_t> BuildSetToneVolumePacket(uint8_t volumePercent)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingToneVolume,
        static_cast<uint8_t>(std::min<uint8_t>(volumePercent, 100)),
        0x50,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetAllowOffListeningModePacket(bool allow)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingAllowOffMode,
        static_cast<uint8_t>(allow ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetListeningModeCyclePacket(uint8_t modeMask)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingListeningCycle,
        static_cast<uint8_t>(modeMask & 0x0Fu),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetPressSpeedPacket(PressSpeed speed)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingPressSpeed,
        static_cast<uint8_t>(speed),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetPressHoldDurationPacket(PressHoldDuration duration)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingPressHoldDuration,
        static_cast<uint8_t>(duration),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetHearingAidPacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingHearingAid,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetMediaAssistPacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingMediaAssist,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildSetHeartRateMonitoringPacket(bool enabled)
{
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        kOpcodeControlCommand,
        0x00,
        kSettingHeartRateMonitoring,
        static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00,
        0x00,
        0x00,
    };
}

std::vector<uint8_t> BuildRequestHeartRatePacket()
{
    return RtBuddy::BuildServiceReportIntervalPacket(
        1, RtBuddy::kLegacyHeartRateCommandService, RtBuddy::kDefaultReportPeriodUs);
}

std::vector<uint8_t> BuildStartSidebandMicPacket()
{
    // AACP_START_AUDIO (sideband microphone stream while A2DP stays stereo).
    return {
        0x04, 0x00, 0x04, 0x00, kOpcodeAudioStream, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x01,
    };
}

std::vector<uint8_t> BuildStopSidebandMicPacket()
{
    // AACP_STOP_AUDIO.
    return {
        0x04, 0x00, 0x04, 0x00, kOpcodeAudioStream, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03, 0x01,
    };
}

std::vector<uint8_t> BuildSetRenamePacket(const std::string &utf8Name)
{
    const size_t nameLen = std::min(utf8Name.size(), kMaxRenameBytes);
    std::vector<uint8_t> packet;
    packet.reserve(8 + nameLen);
    packet.insert(packet.end(), kDataHeader.begin(), kDataHeader.end());
    packet.push_back(kOpcodeRename);
    packet.push_back(0x00);
    packet.push_back(0x01);
    packet.push_back(static_cast<uint8_t>(nameLen));
    packet.insert(
        packet.end(), utf8Name.begin(), utf8Name.begin() + static_cast<std::ptrdiff_t>(nameLen));
    return packet;
}

std::optional<InboundMessage> DecodePacket(std::span<const uint8_t> packet)
{
    if (!HasDataHeader(packet)) {
        return std::nullopt;
    }

    switch (packet[4]) {
    case kOpcodeBattery:
        return DecodeBattery(packet);
    case kOpcodeEarDetection:
        return DecodeEarDetection(packet);
    case kOpcodeControlCommand:
        return DecodeControlCommand(packet);
    case kOpcodeRtBuddySensor: {
        const auto sample = RtBuddy::TryDecodeHeartRatePacket(packet);
        if (!sample.has_value()) {
            return std::nullopt;
        }
        return HeartRateNotification{
            .bpm = sample->bpm,
            .confidence = sample->confidence,
        };
    }
    case kOpcodeMetadata:
        return DecodeMetadata(packet);
    case kOpcodeConversationalSpeech:
        return DecodeConversationalSpeech(packet);
    case kOpcodeHeartRate:
        return DecodeHeartRate(packet);
    default:
        return std::nullopt;
    }
}

bool ApplyInboundMessage(State &state, const InboundMessage &message, bool primaryIsLeft)
{
    const State before = state;

    std::visit(
        Helper::Overloaded{
            [&](const BatteryNotification &battery) {
                state.batteryPrecision = BatteryPrecision::ExactPercent;
                for (const auto &entry : battery.entries) {
                    switch (entry.component) {
                    case ComponentType::Left:
                    case ComponentType::Single:
                        ApplyBatteryPart(state.pods.left, entry);
                        break;
                    case ComponentType::Right:
                        ApplyBatteryPart(state.pods.right, entry);
                        break;
                    case ComponentType::Case:
                        // When the charging case lid shuts, AirPods report the case as
                        // Disconnected; keep the last known 1% case level visible (matching iOS)
                        // until a fresh case reading arrives or Bluetooth disconnects.
                        ApplyBatteryPart(state.caseBox, entry, true);
                        break;
                    }
                }
            },
            [&](const EarDetectionNotification &ear) {
                auto &primaryPod = primaryIsLeft ? state.pods.left : state.pods.right;
                auto &secondaryPod = primaryIsLeft ? state.pods.right : state.pods.left;
                if (ear.primary != EarPlacement::InMotion) {
                    primaryPod.isInEar = ear.primary == EarPlacement::InEar;
                }
                if (ear.secondary != EarPlacement::InMotion) {
                    secondaryPod.isInEar = ear.secondary == EarPlacement::InEar;
                }
                state.caseBox.isBothPodsInCase =
                    ear.primary == EarPlacement::InCase && ear.secondary == EarPlacement::InCase;
                if (!state.pods.left.isInEar && !state.pods.right.isInEar) {
                    state.noiseControl.conversationalAwarenessSpeaking = false;
                    state.noiseControl.conversationalAwarenessLevel = 0;
                }
            },
            [&](const NoiseControlNotification &noise) {
                state.noiseControl.activeMode = noise.mode;
                state.noiseControl.controllable = true;
            },
            [&](const ConversationalAwarenessNotification &awareness) {
                state.noiseControl.conversationalAwareness = awareness.enabled;
                if (!awareness.enabled) {
                    state.noiseControl.conversationalAwarenessSpeaking = false;
                    state.noiseControl.conversationalAwarenessLevel = 0;
                }
                state.noiseControl.controllable = true;
            },
            [&](const ConversationalAwarenessSpeechNotification &speech) {
                if (state.noiseControl.conversationalAwareness == std::optional{false}) {
                    state.noiseControl.conversationalAwarenessSpeaking = false;
                    state.noiseControl.conversationalAwarenessLevel = 0;
                }
                else {
                    state.noiseControl.conversationalAwarenessSpeaking = speech.speaking;
                    state.noiseControl.conversationalAwarenessLevel = speech.level;
                }
                state.noiseControl.controllable = true;
            },
            [&](const FeatureToggleNotification &toggle) {
                state.noiseControl.controllable = true;
                switch (toggle.feature) {
                case ToggleFeature::OneBudAnc:
                    state.noiseControl.oneBudAnc = toggle.enabled;
                    break;
                case ToggleFeature::PersonalizedVolume:
                    state.noiseControl.personalizedVolume = toggle.enabled;
                    break;
                case ToggleFeature::LoudSoundReduction:
                    state.noiseControl.loudSoundReduction = toggle.enabled;
                    break;
                case ToggleFeature::HeadGestures:
                    state.noiseControl.headGestures = toggle.enabled;
                    break;
                case ToggleFeature::AllowOffListeningMode:
                    state.noiseControl.allowOffListeningMode = toggle.enabled;
                    break;
                case ToggleFeature::HearingAid:
                    state.noiseControl.hearingAidEnabled = toggle.enabled;
                    break;
                case ToggleFeature::MediaAssist:
                    state.noiseControl.mediaAssistEnabled = toggle.enabled;
                    break;
                case ToggleFeature::HeartRateMonitoring:
                    state.noiseControl.heartRateMonitoringEnabled = toggle.enabled;
                    if (!toggle.enabled) {
                        state.noiseControl.heartRateStatus = HeartRateStatus::Off;
                        state.noiseControl.heartRateBpm.reset();
                        state.noiseControl.heartRateConfidence.reset();
                        state.noiseControl.heartRateMinBpm.reset();
                        state.noiseControl.heartRateMaxBpm.reset();
                        state.noiseControl.heartRateAvgBpm.reset();
                        state.noiseControl.heartRateHistory.clear();
                    }
                    break;
                }
            },
            [&](const AdaptiveNoiseLevelNotification &adaptive) {
                state.noiseControl.adaptiveNoiseLevel = adaptive.level;
                state.noiseControl.controllable = true;
            },
            [&](const MicrophoneSideNotification &mic) {
                state.noiseControl.microphoneSide = mic.side;
                state.noiseControl.controllable = true;
            },
            [&](const ToneVolumeNotification &tone) {
                state.noiseControl.toneVolume = tone.volume;
                state.noiseControl.controllable = true;
            },
            [&](const ListeningModeCycleNotification &cycle) {
                state.noiseControl.listeningModeCycleMask = cycle.modeMask;
                state.noiseControl.controllable = true;
            },
            [&](const PressSpeedNotification &speed) {
                state.noiseControl.pressSpeed = speed.speed;
                state.noiseControl.controllable = true;
            },
            [&](const PressHoldDurationNotification &hold) {
                state.noiseControl.pressHoldDuration = hold.duration;
                state.noiseControl.controllable = true;
            },
            [&](const HeartRateNotification &hr) {
                state.noiseControl.heartRateStatus = HeartRateStatus::Live;
                state.noiseControl.heartRateBpm = hr.bpm;
                if (hr.confidence > 0) {
                    state.noiseControl.heartRateConfidence = hr.confidence;
                }
                if (!state.noiseControl.heartRateMinBpm.has_value() ||
                    hr.bpm < state.noiseControl.heartRateMinBpm.value())
                {
                    state.noiseControl.heartRateMinBpm = hr.bpm;
                }
                if (!state.noiseControl.heartRateMaxBpm.has_value() ||
                    hr.bpm > state.noiseControl.heartRateMaxBpm.value())
                {
                    state.noiseControl.heartRateMaxBpm = hr.bpm;
                }
                constexpr size_t kMaxHeartRateHistory = 48;
                auto &history = state.noiseControl.heartRateHistory;
                history.push_back(hr.bpm);
                if (history.size() > kMaxHeartRateHistory) {
                    history.erase(
                        history.begin(),
                        history.begin() +
                            static_cast<std::ptrdiff_t>(history.size() - kMaxHeartRateHistory));
                }
                const uint64_t sum = std::accumulate(history.begin(), history.end(), uint64_t{0});
                state.noiseControl.heartRateAvgBpm = static_cast<uint16_t>(
                    (sum + history.size() / 2) / std::max<size_t>(1, history.size()));
                state.noiseControl.controllable = true;
            },
            [&](const HeartRateStatusNotification &hrStatus) {
                state.noiseControl.heartRateStatus = hrStatus.status;
                if (hrStatus.status == HeartRateStatus::Off) {
                    state.noiseControl.heartRateBpm.reset();
                    state.noiseControl.heartRateConfidence.reset();
                    state.noiseControl.heartRateMinBpm.reset();
                    state.noiseControl.heartRateMaxBpm.reset();
                    state.noiseControl.heartRateAvgBpm.reset();
                    state.noiseControl.heartRateHistory.clear();
                }
                state.noiseControl.controllable = true;
            },
            [&](const MetadataNotification &meta) {
                if (const Model resolved = ModelFromModelNumber(meta.modelNumber);
                    resolved != Model::Unknown && state.model == Model::Unknown)
                {
                    state.model = resolved;
                }
                if (!meta.deviceName.isEmpty()) {
                    state.displayName = meta.deviceName;
                }
                if (!meta.firmwareVersion.isEmpty()) {
                    state.firmwareVersion = meta.firmwareVersion;
                }
                if (!meta.serialNumber.isEmpty()) {
                    state.serialNumber = meta.serialNumber;
                }
            },
        },
        message);

    return state != before;
}

} // namespace Core::AirPods::Aap
