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
    AirPods_Pro,
    AirPods_Pro_2,
    AirPods_Pro_2_USB_C,
    AirPods_Pro_3,
    AirPods_Max,
    AirPods_Max_USB_C,
    Powerbeats_3,
    Beats_X,
    Beats_Solo3,
    Beats_Fit_Pro,

    _Max
};

enum class Side : uint32_t { Left, Right };

//
// The identifier Apple's asset catalogue uses for a model: the `ProductType` of
// com.apple.MobileAsset.SharingDeviceAssets, which is where the popup's artwork comes
// from (see Docs/Artwork.md). Models that share a bundle share the identifier. Empty for
// `Unknown`.
//
//
// A device that is one thing with one battery -- over-ear headphones, neckband earphones --
// rather than a pair of buds with a case. Its advertisement still uses the pair's layout,
// carrying the one level on one side; `OnePieceState` in AirPodsState.h reads it out.
//
inline bool IsOnePiece(Model model)
{
    switch (model) {
    case Model::AirPods_Max:
    case Model::AirPods_Max_USB_C:
    case Model::Powerbeats_3:
    case Model::Beats_X:
    case Model::Beats_Solo3:
        return true;
    default:
        return false;
    }
}

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
        return "AirPodsMax1,2";
    case Model::Powerbeats_3:
        return "PowerBeats3,1";
    case Model::Beats_X:
        return "BeatsX1,1";
    case Model::Beats_Solo3:
        return "BeatsSolo3,1";
    case Model::Beats_Fit_Pro:
        return "BeatsFitPro1,1";
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
    case Core::AirPods::Model::Powerbeats_3:
        return "Powerbeats 3";
    case Core::AirPods::Model::Beats_X:
        return "BeatsX";
    case Core::AirPods::Model::Beats_Solo3:
        return "BeatsSolo3";
    case Core::AirPods::Model::Beats_Fit_Pro:
        return "Beats Fit Pro";
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
