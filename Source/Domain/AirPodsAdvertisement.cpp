// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/AirPodsAdvertisement.h"

#include "Source/Support/Assert.h"

namespace Core::AirPods::Details {

bool Advertisement::IsDesiredAdv(const Bluetooth::ReceivedAdvertisement &data)
{
    auto iter = data.manufacturerDataMap.find(AppleCP::VendorId);
    if (iter == data.manufacturerDataMap.end()) {
        return false;
    }
    return AppleCP::AirPods::IsValid(iter->second);
}

Advertisement::Advertisement(const Bluetooth::ReceivedAdvertisement &data)
    : _data{data}, _protocol{[&] {
          APB_ASSERT(IsDesiredAdv(data));
          auto parsed = AppleCP::AirPods::Parse(GetMfrData());
          APB_ASSERT(parsed.has_value());
          return std::move(parsed.value());
      }()}
{
    _state.model = _protocol.GetModel();
    _state.colour = _protocol.GetColourCode();
    _state.side = _protocol.GetBroadcastedSide();
    _state.batteryPrecision = BatteryPrecision::CoarseTenPercent;

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
    _state.lidOpenCounter = _protocol.GetLidOpenCounter();

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
    const auto desensitized = _protocol.Desensitize();
    const auto &bytes = desensitized.RawBytes();
    return {bytes.begin(), bytes.end()};
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
