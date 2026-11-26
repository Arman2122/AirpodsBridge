// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <vector>

#include "Source/Domain/AirPodsState.h"
#include "Source/Domain/AppleCP.h"
#include "Source/Domain/BluetoothTypes.h"

namespace Core::AirPods::Details {

//
// A single decoded proximity-pairing advertisement.
//
// Operates strictly on `Bluetooth::ReceivedAdvertisement` value data from `apb_domain`,
// keeping the entire decode pipeline free of platform or core dependencies.
//
class Advertisement
{
public:
    using AddressType = decltype(Bluetooth::ReceivedAdvertisement::address);
    using TimestampType = Bluetooth::ReceivedAdvertisement::Timestamp;

    struct AdvState : AirPods::State {
        Side side{Side::Left};
        uint8_t lidOpenCounter{0};
    };

    static bool IsDesiredAdv(const Bluetooth::ReceivedAdvertisement &data);

    explicit Advertisement(const Bluetooth::ReceivedAdvertisement &data);

    int16_t GetRssi() const;
    const TimestampType &GetTimestamp() const;
    AddressType GetAddress() const;
    std::vector<uint8_t> GetDesensitizedData() const;
    const AdvState &GetAdvState() const;

private:
    Bluetooth::ReceivedAdvertisement _data;
    AppleCP::AirPods _protocol;
    AdvState _state;

    const std::vector<uint8_t> &GetMfrData() const;
};

} // namespace Core::AirPods::Details
