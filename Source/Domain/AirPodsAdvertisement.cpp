// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/AirPodsAdvertisement.h"

#include <cstring>

#include "Source/Support/Assert.h"

namespace Core::AirPods::Details {

bool Advertisement::IsDesiredAdv(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    auto iter = data.manufacturerDataMap.find(AppleCP::VendorId);
    if (iter == data.manufacturerDataMap.end()) {
        return false;
    }
    return AppleCP::AirPods::IsValid(iter->second);
}

Advertisement::Advertisement(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    APB_ASSERT(IsDesiredAdv(data));
    _data = data;

    auto protocol = AppleCP::As<AppleCP::AirPods>(GetMfrData());
    APB_ASSERT(protocol.has_value());
    _protocol = std::move(protocol.value());

    _state.model = _protocol.GetModel();
    _state.colour = _protocol.GetColourCode();
    _state.side = _protocol.GetBroadcastedSide();

    _state.pods.left.battery = _protocol.GetLeftBattery();
    _state.pods.left.isCharging = _protocol.IsLeftCharging();
    _state.pods.left.isInEar = _protocol.IsLeftInEar();

    _state.pods.right.battery = _protocol.GetRightBattery();
    _state.pods.right.isCharging = _protocol.IsRightCharging();
    _state.pods.right.isInEar = _protocol.IsRightInEar();

    _state.caseBox.battery = _protocol.GetCaseBattery();
    _state.caseBox.isCharging = _protocol.IsCaseCharging();
    _state.caseBox.isBothPodsInCase = _protocol.IsBothPodsInCase();
    _state.caseBox.isLidOpened = _protocol.IsLidOpened();

    if (_state.pods.left.battery.Available()) {
        _state.pods.left.battery = _state.pods.left.battery.Value() * 10;
    }
    if (_state.pods.right.battery.Available()) {
        _state.pods.right.battery = _state.pods.right.battery.Value() * 10;
    }
    if (_state.caseBox.battery.Available()) {
        _state.caseBox.battery = _state.caseBox.battery.Value() * 10;
    }
}

int16_t Advertisement::GetRssi() const
{
    return _data.rssi;
}

const Advertisement::TimestampType &Advertisement::GetTimestamp() const
{
    return _data.timestamp;
}

auto Advertisement::GetAddress() const -> AddressType
{
    return _data.address;
}

std::vector<uint8_t> Advertisement::GetDesensitizedData() const
{
    auto desensitizedData = _protocol.Desensitize();
    std::vector<uint8_t> result(sizeof(desensitizedData), 0);
    std::memcpy(result.data(), &desensitizedData, sizeof(desensitizedData));
    return result;
}

auto Advertisement::GetAdvState() const -> const AdvState &
{
    return _state;
}

const std::vector<uint8_t> &Advertisement::GetMfrData() const
{
    auto iter = _data.manufacturerDataMap.find(AppleCP::VendorId);
    APB_ASSERT(iter != _data.manufacturerDataMap.end());
    return iter->second;
}

} // namespace Core::AirPods::Details
