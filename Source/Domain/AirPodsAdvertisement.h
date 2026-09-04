// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <vector>

#include "Source/Core/Bluetooth.h"
#include "Source/Domain/AirPodsState.h"
#include "Source/Domain/AppleCP.h"

namespace Core::AirPods::Details {

//
// A single decoded proximity-pairing advertisement.
//
// Only the `ReceivedData` value type is borrowed from the Bluetooth layer; nothing here
// starts a scan or touches the platform, which keeps the decode path unit-testable.
//
class Advertisement
{
public:
    using AddressType = decltype(Bluetooth::AdvertisementWatcher::ReceivedData::address);
    using TimestampType = decltype(Bluetooth::AdvertisementWatcher::ReceivedData::timestamp);

    struct AdvState : AirPods::State {
        Side side;
    };

    static bool IsDesiredAdv(const Bluetooth::AdvertisementWatcher::ReceivedData &data);

    Advertisement(const Bluetooth::AdvertisementWatcher::ReceivedData &data);

    int16_t GetRssi() const;
    const TimestampType &GetTimestamp() const;
    AddressType GetAddress() const;
    std::vector<uint8_t> GetDesensitizedData() const;
    const AdvState &GetAdvState() const;

private:
    Bluetooth::AdvertisementWatcher::ReceivedData _data;
    AppleCP::AirPods _protocol;
    AdvState _state;

    const std::vector<uint8_t> &GetMfrData() const;
};

} // namespace Core::AirPods::Details
