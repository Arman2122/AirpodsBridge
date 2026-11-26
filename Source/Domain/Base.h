// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <optional>

#include "Source/Support/Helper.h"
#include "Source/Support/Logger.h"

namespace Core::AirPods {

class Battery
{
public:
    using ValueType = uint32_t;

    Battery() = default;
    inline Battery(ValueType value) : _optValue{value} {}

    bool operator==(const Battery &rhs) const = default;

    inline bool Available() const
    {
        return _optValue.has_value();
    }

    inline ValueType Value() const
    {
        if (!_optValue.has_value()) {
            LOG(Warn, "Trying to get the battery value but unavailable.");
            return 0;
        }
        return _optValue.value();
    }

    // What counts as low is a user setting; the default is the level Apple warns at.
    static constexpr ValueType kDefaultLowThreshold = 20;

    inline bool IsLowBattery(ValueType threshold = kDefaultLowThreshold) const
    {
        if (!_optValue.has_value()) {
            LOG(Warn, "Trying to determine that the battery is low but unavailable.");
            return false;
        }
        return _optValue.value() <= threshold;
    }

private:
    std::optional<ValueType> _optValue;
};

enum class Model : uint32_t {
    Unknown = 0,
    AirPods_1,
    AirPods_2,
    AirPods_3,
    AirPods_4,
    AirPods_4_ANC,
    AirPods_5,
    AirPods_5_WCC,
    AirPods_Pro,
    AirPods_Pro_2,
    AirPods_Pro_2_USB_C,
    AirPods_Pro_3,
    AirPods_Max,
    AirPods_Max_USB_C,
    AirPods_Max_2,
    Powerbeats_3,
    Powerbeats_4,
    Powerbeats_Pro,
    Powerbeats_Pro_2,
    Powerbeats_Fit,
    Beats_X,
    Beats_Flex,
    Beats_Solo3,
    Beats_Solo_Pro,
    Beats_Solo_4,
    Beats_Solo_Buds,
    Beats_Studio3,
    Beats_Studio_Pro,
    Beats_Studio_Buds,
    Beats_Studio_Buds_Plus,
    Beats_Fit_Pro,
    Beats_360,

    _Max
};

// Apple's Bluetooth SIG Company Identifier (0x004C) and model lookup shared by both BLE
// Continuity decoding and Windows PnP `DEVPKEY_Bluetooth_DevicePID` recognition.
constexpr uint16_t kAppleVendorId = 0x004C;

constexpr Model ModelFromProductId(uint16_t productId) noexcept
{
    switch (productId) {
    case 0x2002:
        return Model::AirPods_1;
    case 0x200F:
        return Model::AirPods_2;
    case 0x2013:
        return Model::AirPods_3;
    case 0x2019:
        return Model::AirPods_4;
    case 0x201B:
        return Model::AirPods_4_ANC;
    case 0x2036:
        return Model::AirPods_5;
    case 0x2030:
        return Model::AirPods_5_WCC;
    case 0x200E:
        return Model::AirPods_Pro;
    case 0x2014:
        return Model::AirPods_Pro_2;
    case 0x2024:
        return Model::AirPods_Pro_2_USB_C;
    case 0x2027:
        return Model::AirPods_Pro_3;
    case 0x200A:
        return Model::AirPods_Max;
    case 0x201F:
        return Model::AirPods_Max_USB_C;
    case 0x202D:
        return Model::AirPods_Max_2;
    case 0x2003:
        return Model::Powerbeats_3;
    case 0x200D:
        return Model::Powerbeats_4;
    case 0x200B:
        return Model::Powerbeats_Pro;
    case 0x201D:
        return Model::Powerbeats_Pro_2;
    case 0x202F:
        return Model::Powerbeats_Fit;
    case 0x2005:
        return Model::Beats_X;
    case 0x2010:
        return Model::Beats_Flex;
    case 0x2006:
        return Model::Beats_Solo3;
    case 0x200C:
        return Model::Beats_Solo_Pro;
    case 0x2025:
        return Model::Beats_Solo_4;
    case 0x2026:
        return Model::Beats_Solo_Buds;
    case 0x2009:
        return Model::Beats_Studio3;
    case 0x2017:
        return Model::Beats_Studio_Pro;
    case 0x2011:
        return Model::Beats_Studio_Buds;
    case 0x2016:
        return Model::Beats_Studio_Buds_Plus;
    case 0x2012:
        return Model::Beats_Fit_Pro;
    case 0x2038:
        return Model::Beats_360;
    default:
        return Model::Unknown;
    }
}

// Resolves Apple's hardware model number (e.g. "A3063" from AAP 0x1D metadata) as a
// fallback/refinement when Bluetooth PID lookup is unavailable or generic.
inline Model ModelFromModelNumber(const QString &modelNumber)
{
    const QString key = modelNumber.trimmed().toUpper();
    if (key == QLatin1String{"A1523"} || key == QLatin1String{"A1722"}) {
        return Model::AirPods_1;
    }
    if (key == QLatin1String{"A2032"} || key == QLatin1String{"A2031"}) {
        return Model::AirPods_2;
    }
    if (key == QLatin1String{"A2565"} || key == QLatin1String{"A2564"}) {
        return Model::AirPods_3;
    }
    if (key == QLatin1String{"A3053"} || key == QLatin1String{"A3050"} ||
        key == QLatin1String{"A3054"})
    {
        return Model::AirPods_4;
    }
    if (key == QLatin1String{"A3056"} || key == QLatin1String{"A3055"} ||
        key == QLatin1String{"A3057"})
    {
        return Model::AirPods_4_ANC;
    }
    if (key == QLatin1String{"A3531"} || key == QLatin1String{"A3532"} ||
        key == QLatin1String{"A3533"})
    {
        return Model::AirPods_5;
    }
    if (key == QLatin1String{"A3439"} || key == QLatin1String{"A3440"} ||
        key == QLatin1String{"A3441"})
    {
        return Model::AirPods_5_WCC;
    }
    if (key == QLatin1String{"A2084"} || key == QLatin1String{"A2083"}) {
        return Model::AirPods_Pro;
    }
    if (key == QLatin1String{"A2931"} || key == QLatin1String{"A2699"} ||
        key == QLatin1String{"A2698"})
    {
        return Model::AirPods_Pro_2;
    }
    if (key == QLatin1String{"A3047"} || key == QLatin1String{"A3048"} ||
        key == QLatin1String{"A3049"})
    {
        return Model::AirPods_Pro_2_USB_C;
    }
    if (key == QLatin1String{"A3063"} || key == QLatin1String{"A3064"} ||
        key == QLatin1String{"A3065"})
    {
        return Model::AirPods_Pro_3;
    }
    if (key == QLatin1String{"A2096"}) {
        return Model::AirPods_Max;
    }
    if (key == QLatin1String{"A3184"}) {
        return Model::AirPods_Max_USB_C;
    }
    if (key == QLatin1String{"A3454"}) {
        return Model::AirPods_Max_2;
    }
    if (key == QLatin1String{"A1747"}) {
        return Model::Powerbeats_3;
    }
    if (key == QLatin1String{"A2015"}) {
        return Model::Powerbeats_4;
    }
    if (key == QLatin1String{"A2047"} || key == QLatin1String{"A2048"} ||
        key == QLatin1String{"A2078"})
    {
        return Model::Powerbeats_Pro;
    }
    if (key == QLatin1String{"A3157"} || key == QLatin1String{"A3158"} ||
        key == QLatin1String{"A3160"})
    {
        return Model::Powerbeats_Pro_2;
    }
    if (key == QLatin1String{"A3256"} || key == QLatin1String{"A3257"}) {
        return Model::Powerbeats_Fit;
    }
    if (key == QLatin1String{"A1763"}) {
        return Model::Beats_X;
    }
    if (key == QLatin1String{"A2295"}) {
        return Model::Beats_Flex;
    }
    if (key == QLatin1String{"A1796"}) {
        return Model::Beats_Solo3;
    }
    if (key == QLatin1String{"A1881"}) {
        return Model::Beats_Solo_Pro;
    }
    if (key == QLatin1String{"A3140"}) {
        return Model::Beats_Solo_4;
    }
    if (key == QLatin1String{"A3150"} || key == QLatin1String{"A3151"}) {
        return Model::Beats_Solo_Buds;
    }
    if (key == QLatin1String{"A1914"}) {
        return Model::Beats_Studio3;
    }
    if (key == QLatin1String{"A2924"}) {
        return Model::Beats_Studio_Pro;
    }
    if (key == QLatin1String{"A2512"} || key == QLatin1String{"A2513"}) {
        return Model::Beats_Studio_Buds;
    }
    if (key == QLatin1String{"A2871"} || key == QLatin1String{"A2872"}) {
        return Model::Beats_Studio_Buds_Plus;
    }
    if (key == QLatin1String{"A2576"} || key == QLatin1String{"A2577"}) {
        return Model::Beats_Fit_Pro;
    }
    return Model::Unknown;
}

enum class Side : uint32_t { Left, Right };

//
// Listening / noise control mode reported or requested over the Apple Accessory Protocol
// (AAP). BLE advertisements do not carry this; it activates when an AAP control transport
// is connected.
//
enum class NoiseControlMode : uint32_t {
    Off = 0,
    NoiseCancellation = 1,
    Transparency = 2,
    Adaptive = 3,
};

enum class MicrophoneSide : uint8_t {
    Auto = 0,
    AlwaysLeft = 1,
    AlwaysRight = 2,
};

enum class PressSpeed : uint8_t {
    Default = 0,
    Slower = 1,
    Slowest = 2,
};

enum class PressHoldDuration : uint8_t {
    Default = 0,
    Shorter = 1,
    Shortest = 2,
};

namespace ListeningCycleBit {
inline constexpr uint8_t kOff = 0x01;
inline constexpr uint8_t kNoiseCancellation = 0x02;
inline constexpr uint8_t kTransparency = 0x04;
inline constexpr uint8_t kAdaptive = 0x08;
} // namespace ListeningCycleBit

//
// How finely battery levels are reported by the active source.
// BLE proximity-pairing advertisements encode battery in 10% buckets; AAP reports 1% steps.
//
enum class BatteryPrecision : uint32_t {
    CoarseTenPercent = 0,
    ExactPercent = 1,
};

//
// Active Windows Bluetooth audio profile for the connected device.
//
enum class AudioProfile : uint32_t {
    Unknown = 0,
    Stereo = 1,    // A2DP high-quality playback
    HandsFree = 2, // HFP/AG call audio (microphone active)
};

//
// Live optical heart-rate sensor lifecycle for AirPods Pro 3 (RTBuddy over AAP 0x17).
//
enum class HeartRateStatus : uint8_t {
    Off = 0,
    Starting = 1,
    AcquiringLock = 2,
    Live = 3,
    WaitingForEar = 4,
    NoSignal = 5,
};

enum class HeartRateZone : uint8_t {
    Resting = 0,  // < 65 BPM
    Normal = 1,   // 65..99 BPM
    Elevated = 2, // 100..124 BPM
    Cardio = 3,   // 125..154 BPM
    Peak = 4,     // >= 155 BPM
};

constexpr HeartRateZone HeartRateZoneForBpm(uint16_t bpm) noexcept
{
    if (bpm < 65) {
        return HeartRateZone::Resting;
    }
    if (bpm < 100) {
        return HeartRateZone::Normal;
    }
    if (bpm < 125) {
        return HeartRateZone::Elevated;
    }
    if (bpm < 155) {
        return HeartRateZone::Cardio;
    }
    return HeartRateZone::Peak;
}

//
// Hardware capabilities per model: drives one-piece presentation, automation policies,
// and which noise-control / listening controls appear once an AAP transport is attached.
//
struct DeviceCapabilities {
    bool isOnePiece{false};
    bool supportsNoiseCancellation{false};
    bool supportsTransparency{false};
    bool supportsAdaptiveAudio{false};
    bool supportsConversationalAwareness{false};
    bool supportsSpatialAudio{false};
    bool supportsStereoA2dpMic{false};
    bool supportsHearingAid{false};
    bool supportsHeartRate{false};
    bool supportsAllowOffMode{false};

    constexpr bool SupportsNoiseControl() const noexcept
    {
        return supportsNoiseCancellation || supportsTransparency || supportsAdaptiveAudio;
    }

    bool operator==(const DeviceCapabilities &) const = default;
};

constexpr DeviceCapabilities CapabilitiesFor(Model model) noexcept
{
    switch (model) {
    case Model::AirPods_Pro:
        return {
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsSpatialAudio = true,
        };
    case Model::AirPods_Pro_2:
    case Model::AirPods_Pro_2_USB_C:
        return {
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsAdaptiveAudio = true,
            .supportsConversationalAwareness = true,
            .supportsSpatialAudio = true,
            .supportsStereoA2dpMic = true,
            .supportsHearingAid = true,
            .supportsAllowOffMode = true,
        };
    case Model::AirPods_Pro_3:
        return {
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsAdaptiveAudio = true,
            .supportsConversationalAwareness = true,
            .supportsSpatialAudio = true,
            .supportsStereoA2dpMic = true,
            .supportsHearingAid = true,
            .supportsHeartRate = true,
            .supportsAllowOffMode = true,
        };
    case Model::AirPods_4_ANC:
    case Model::AirPods_5:
    case Model::AirPods_5_WCC:
        return {
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsAdaptiveAudio = true,
            .supportsConversationalAwareness = true,
            .supportsSpatialAudio = true,
            .supportsStereoA2dpMic = true,
            .supportsAllowOffMode = true,
        };
    case Model::AirPods_Max:
        return {
            .isOnePiece = true,
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsSpatialAudio = true,
        };
    case Model::AirPods_Max_USB_C:
        return {
            .isOnePiece = true,
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsSpatialAudio = true,
            .supportsStereoA2dpMic = true,
        };
    case Model::AirPods_Max_2:
        return {
            .isOnePiece = true,
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsAdaptiveAudio = true,
            .supportsConversationalAwareness = true,
            .supportsSpatialAudio = true,
            .supportsStereoA2dpMic = true,
        };
    case Model::Powerbeats_Pro_2:
        return {
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsAdaptiveAudio = true,
            .supportsSpatialAudio = true,
            .supportsStereoA2dpMic = true,
            .supportsHeartRate = true,
        };
    case Model::Powerbeats_Fit:
    case Model::Beats_Fit_Pro:
    case Model::Beats_Studio_Buds_Plus:
        return {
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsSpatialAudio = true,
        };
    case Model::Beats_Studio_Buds:
        return {
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
        };
    case Model::Beats_Solo_Pro:
        return {
            .isOnePiece = true,
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
        };
    case Model::Beats_Studio3:
        return {
            .isOnePiece = true,
            .supportsNoiseCancellation = true,
        };
    case Model::Beats_Studio_Pro:
        return {
            .isOnePiece = true,
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsSpatialAudio = true,
        };
    case Model::Beats_360:
        return {
            .isOnePiece = true,
            .supportsNoiseCancellation = true,
            .supportsTransparency = true,
            .supportsAdaptiveAudio = true,
            .supportsSpatialAudio = true,
        };
    case Model::Beats_Solo_4:
        return {
            .isOnePiece = true,
            .supportsSpatialAudio = true,
        };
    case Model::AirPods_3:
        return {
            .supportsSpatialAudio = true,
        };
    case Model::AirPods_4:
        return {
            .supportsSpatialAudio = true,
            .supportsStereoA2dpMic = true,
        };
    case Model::Powerbeats_3:
    case Model::Powerbeats_4:
    case Model::Beats_X:
    case Model::Beats_Flex:
    case Model::Beats_Solo3:
        return {
            .isOnePiece = true,
        };
    case Model::Powerbeats_Pro:
    case Model::Beats_Solo_Buds:
    default:
        return {};
    }
}

//
// A device that is one thing with one battery -- over-ear headphones, neckband earphones --
// rather than a pair of buds with a case. Its advertisement still uses the pair's layout,
// carrying the one level on one side; `OnePieceState` in AirPodsState.h reads it out.
//
constexpr bool IsOnePiece(Model model) noexcept
{
    return CapabilitiesFor(model).isOnePiece;
}

//
// The identifier Apple's asset catalogue uses for a model: the `ProductType` of
// com.apple.MobileAsset.SharingDeviceAssets, which is where the popup's artwork comes
// from (see Docs/Artwork.md). Models that share a bundle share the identifier. Empty for
// `Unknown`.
//

inline QString AppleProductType(Model model)
{
    switch (model) {
    case Model::AirPods_1:
        return "AirPods1,1";
    case Model::AirPods_2:
        return "AirPods1,2";
    case Model::AirPods_3:
        return "AirPods1,3";
    case Model::AirPods_4:
        return "AirPods1,4";
    case Model::AirPods_4_ANC:
        return "AirPods1,5";
    case Model::AirPods_5:
        return "AirPods3,7";
    case Model::AirPods_5_WCC:
        return "AirPods3,8";
    case Model::AirPods_Pro:
        return "AirPodsPro1,1";
    case Model::AirPods_Pro_2:
    case Model::AirPods_Pro_2_USB_C:
        return "AirPodsPro1,2";
    case Model::AirPods_Pro_3:
        return "AirPodsPro1,3";
    case Model::AirPods_Max:
        return "AirPodsMax1,1";
    case Model::AirPods_Max_USB_C:
    case Model::AirPods_Max_2:
        return "AirPodsMax1,2";
    case Model::Powerbeats_3:
        return "PowerBeats3,1";
    case Model::Powerbeats_4:
        return "Powerbeats4,1";
    case Model::Powerbeats_Pro:
        return "PowerbeatsPro1,1";
    case Model::Powerbeats_Pro_2:
        return "PowerbeatsPro2,1";
    case Model::Powerbeats_Fit:
    case Model::Beats_Fit_Pro:
        return "BeatsFitPro1,1";
    case Model::Beats_X:
        return "BeatsX1,1";
    case Model::Beats_Flex:
        return "BeatsX2,1";
    case Model::Beats_Solo3:
        return "BeatsSolo3,1";
    case Model::Beats_Solo_Pro:
        return "BeatsSoloPro1,1";
    case Model::Beats_Solo_4:
        return "BeatsSolo4,1";
    case Model::Beats_Solo_Buds:
        return "BeatsSoloBuds1,1";
    case Model::Beats_Studio3:
        return "BeatsStudio3,2";
    case Model::Beats_Studio_Pro:
        return "BeatsStudioPro1,1";
    case Model::Beats_Studio_Buds:
        return "BeatsStudioBuds1,1";
    case Model::Beats_Studio_Buds_Plus:
        return "BeatsStudioBuds1,2";
    case Model::Beats_360:
        return "Beats3601,1";
    default:
        return {};
    }
}

} // namespace Core::AirPods

template <>
inline QString Helper::ToString<Core::AirPods::Model>(const Core::AirPods::Model &value)
{
    switch (value) {
    case Core::AirPods::Model::AirPods_1:
        return "AirPods 1";
    case Core::AirPods::Model::AirPods_2:
        return "AirPods 2";
    case Core::AirPods::Model::AirPods_3:
        return "AirPods 3";
    case Core::AirPods::Model::AirPods_4:
        return "AirPods 4";
    case Core::AirPods::Model::AirPods_4_ANC:
        return "AirPods 4 (ANC)";
    case Core::AirPods::Model::AirPods_5:
        return "AirPods 5";
    case Core::AirPods::Model::AirPods_5_WCC:
        return "AirPods 5 (Wireless Case)";
    case Core::AirPods::Model::AirPods_Pro:
        return "AirPods Pro";
    case Core::AirPods::Model::AirPods_Pro_2:
        return "AirPods Pro 2";
    case Core::AirPods::Model::AirPods_Pro_2_USB_C:
        return "AirPods Pro 2 (USB-C)";
    case Core::AirPods::Model::AirPods_Pro_3:
        return "AirPods Pro 3";
    case Core::AirPods::Model::AirPods_Max:
        return "AirPods Max";
    case Core::AirPods::Model::AirPods_Max_USB_C:
        return "AirPods Max (USB-C)";
    case Core::AirPods::Model::AirPods_Max_2:
        return "AirPods Max 2";
    case Core::AirPods::Model::Powerbeats_3:
        return "Powerbeats 3";
    case Core::AirPods::Model::Powerbeats_4:
        return "Powerbeats";
    case Core::AirPods::Model::Powerbeats_Pro:
        return "Powerbeats Pro";
    case Core::AirPods::Model::Powerbeats_Pro_2:
        return "Powerbeats Pro 2";
    case Core::AirPods::Model::Powerbeats_Fit:
        return "Powerbeats Fit";
    case Core::AirPods::Model::Beats_X:
        return "BeatsX";
    case Core::AirPods::Model::Beats_Flex:
        return "Beats Flex";
    case Core::AirPods::Model::Beats_Solo3:
        return "Beats Solo3";
    case Core::AirPods::Model::Beats_Solo_Pro:
        return "Beats Solo Pro";
    case Core::AirPods::Model::Beats_Solo_4:
        return "Beats Solo 4";
    case Core::AirPods::Model::Beats_Solo_Buds:
        return "Beats Solo Buds";
    case Core::AirPods::Model::Beats_Studio3:
        return "Beats Studio3";
    case Core::AirPods::Model::Beats_Studio_Pro:
        return "Beats Studio Pro";
    case Core::AirPods::Model::Beats_Studio_Buds:
        return "Beats Studio Buds";
    case Core::AirPods::Model::Beats_Studio_Buds_Plus:
        return "Beats Studio Buds +";
    case Core::AirPods::Model::Beats_Fit_Pro:
        return "Beats Fit Pro";
    case Core::AirPods::Model::Beats_360:
        return "Beats 360";
    default:
        return "Unknown";
    }
}

template <>
inline QString Helper::ToString<Core::AirPods::Side>(const Core::AirPods::Side &value)
{
    switch (value) {
    case Core::AirPods::Side::Left:
        return "Left";
    case Core::AirPods::Side::Right:
        return "Right";
    default:
        return "Unknown";
    }
}

template <>
inline QString
Helper::ToString<Core::AirPods::NoiseControlMode>(const Core::AirPods::NoiseControlMode &value)
{
    switch (value) {
    case Core::AirPods::NoiseControlMode::Off:
        return "Off";
    case Core::AirPods::NoiseControlMode::NoiseCancellation:
        return "Noise Cancellation";
    case Core::AirPods::NoiseControlMode::Transparency:
        return "Transparency";
    case Core::AirPods::NoiseControlMode::Adaptive:
        return "Adaptive";
    default:
        return "Unknown";
    }
}
