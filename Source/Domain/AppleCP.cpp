// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/AppleCP.h"

#include <algorithm>

namespace Core::AppleCP {

namespace {

// Wire byte offsets within a 27-byte Proximity Pairing TLV record:
//   [0]      packet type (0x07)
//   [1]      remaining length (0x19 = 25)
//   [2]      pairing state / unknown
//   [3..4]   model ID (little-endian uint16)
//   [5]      status flags (in-ear, both-in-case, broadcast side)
//   [6]      pod battery nibbles (low = broadcasting pod, high = other pod; 0..10 valid)
//   [7]      case battery (low nibble) + charging flags (bits 4..6)
//   [8]      lid counter (bits 0..2) + lid closed (bit 3)
//   [9]      colour code (Apple CL)
//   [10]     reserved
//   [11..26] encrypted / hashed payload (redacted by Desensitize)
constexpr size_t kOffsetType = 0;
constexpr size_t kOffsetLength = 1;
constexpr size_t kOffsetModelLo = 3;
constexpr size_t kOffsetModelHi = 4;
constexpr size_t kOffsetStatus = 5;
constexpr size_t kOffsetPodsBattery = 6;
constexpr size_t kOffsetCaseAndCharge = 7;
constexpr size_t kOffsetLid = 8;
constexpr size_t kOffsetColour = 9;

constexpr uint8_t kStatusCurrInEar = 1u << 1;
constexpr uint8_t kStatusBothInCase = 1u << 2;
constexpr uint8_t kStatusAnotInEar = 1u << 3;
constexpr uint8_t kStatusBroadcastLeft = 1u << 5;

constexpr uint8_t kChargeCurr = 1u << 4;
constexpr uint8_t kChargeAnot = 1u << 5;
constexpr uint8_t kChargeCase = 1u << 6;

constexpr uint8_t kLidClosed = 1u << 3;

std::optional<std::array<uint8_t, AirPods::kPacketSize>>
FindProximityPairingRecord(std::span<const uint8_t> data)
{
    size_t offset = 0;
    while (offset + 2 <= data.size()) {
        const uint8_t type = data[offset + kOffsetType];
        const uint8_t length = data[offset + kOffsetLength];
        const size_t recordSize = 2u + static_cast<size_t>(length);
        if (recordSize > data.size() - offset) {
            return std::nullopt;
        }

        if (type == static_cast<uint8_t>(PacketType::ProximityPairing) &&
            length == AirPods::kExpectedRemainingLength)
        {
            std::array<uint8_t, AirPods::kPacketSize> record{};
            std::copy_n(
                data.begin() + static_cast<std::ptrdiff_t>(offset), AirPods::kPacketSize,
                record.begin());
            return record;
        }

        // Advance to the next Continuity TLV sub-record. Zero-length records still move
        // forward by the 2-byte header so malformed packets can never stall the loop.
        offset += recordSize;
    }

    return std::nullopt;
}

} // namespace

AirPods::AirPods(std::array<uint8_t, kPacketSize> raw) noexcept : _raw{raw} {}

bool AirPods::IsValid(std::span<const uint8_t> data)
{
    return FindProximityPairingRecord(data).has_value();
}

std::optional<AirPods> AirPods::Parse(std::span<const uint8_t> data)
{
    auto record = FindProximityPairingRecord(data);
    if (!record.has_value()) {
        return std::nullopt;
    }
    return AirPods{record.value()};
}

Core::AirPods::Model AirPods::GetModel(uint16_t modelId)
{
    return Core::AirPods::ModelFromProductId(modelId);
}

Core::AirPods::Side AirPods::GetBroadcastedSide() const
{
    return (_raw[kOffsetStatus] & kStatusBroadcastLeft) != 0 ? Core::AirPods::Side::Left
                                                             : Core::AirPods::Side::Right;
}

bool AirPods::IsLeftBroadcasted() const
{
    return GetBroadcastedSide() == Core::AirPods::Side::Left;
}

bool AirPods::IsRightBroadcasted() const
{
    return GetBroadcastedSide() == Core::AirPods::Side::Right;
}

Core::AirPods::Model AirPods::GetModel() const
{
    const uint16_t modelId =
        static_cast<uint16_t>(_raw[kOffsetModelLo]) |
        static_cast<uint16_t>(static_cast<uint16_t>(_raw[kOffsetModelHi]) << 8);
    return GetModel(modelId);
}

Core::AirPods::Battery AirPods::GetLeftBattery() const
{
    const uint8_t curr = _raw[kOffsetPodsBattery] & 0x0Fu;
    const uint8_t anot = (_raw[kOffsetPodsBattery] >> 4) & 0x0Fu;
    const uint8_t val = IsLeftBroadcasted() ? curr : anot;
    return val <= 10 ? Core::AirPods::Battery{val} : Core::AirPods::Battery{};
}

Core::AirPods::Battery AirPods::GetRightBattery() const
{
    const uint8_t curr = _raw[kOffsetPodsBattery] & 0x0Fu;
    const uint8_t anot = (_raw[kOffsetPodsBattery] >> 4) & 0x0Fu;
    const uint8_t val = IsRightBroadcasted() ? curr : anot;
    return val <= 10 ? Core::AirPods::Battery{val} : Core::AirPods::Battery{};
}

Core::AirPods::Battery AirPods::GetCaseBattery() const
{
    const uint8_t val = _raw[kOffsetCaseAndCharge] & 0x0Fu;
    return val <= 10 ? Core::AirPods::Battery{val} : Core::AirPods::Battery{};
}

bool AirPods::IsLeftCharging() const
{
    const uint8_t mask = IsLeftBroadcasted() ? kChargeCurr : kChargeAnot;
    return (_raw[kOffsetCaseAndCharge] & mask) != 0;
}

bool AirPods::IsRightCharging() const
{
    const uint8_t mask = IsRightBroadcasted() ? kChargeCurr : kChargeAnot;
    return (_raw[kOffsetCaseAndCharge] & mask) != 0;
}

bool AirPods::IsBothPodsInCase() const
{
    return (_raw[kOffsetStatus] & kStatusBothInCase) != 0;
}

bool AirPods::IsLidOpened() const
{
    return (_raw[kOffsetLid] & kLidClosed) == 0;
}

uint8_t AirPods::GetLidOpenCounter() const
{
    return static_cast<uint8_t>(_raw[kOffsetLid] & 0x07u);
}

uint8_t AirPods::GetColourCode() const
{
    return _raw[kOffsetColour];
}

bool AirPods::IsCaseCharging() const
{
    return (_raw[kOffsetCaseAndCharge] & kChargeCase) != 0;
}

bool AirPods::IsLeftInEar() const
{
    // If it's charging, the "ear" bit can still be set in one of the dual advertisements;
    // suppress it so charging buds in the case never read as worn.
    const uint8_t mask = IsLeftBroadcasted() ? kStatusCurrInEar : kStatusAnotInEar;
    return !IsLeftCharging() && (_raw[kOffsetStatus] & mask) != 0;
}

bool AirPods::IsRightInEar() const
{
    const uint8_t mask = IsRightBroadcasted() ? kStatusCurrInEar : kStatusAnotInEar;
    return !IsRightCharging() && (_raw[kOffsetStatus] & mask) != 0;
}

AirPods AirPods::Desensitize() const
{
    auto result = *this;

    // Bytes 11..26 carry a rotating encrypted/hashed payload that may identify the user.
    std::fill(result._raw.begin() + kEncryptedPayloadOffset, result._raw.end(), uint8_t{0});
    return result;
}

const std::array<uint8_t, AirPods::kPacketSize> &AirPods::RawBytes() const noexcept
{
    return _raw;
}

} // namespace Core::AppleCP
