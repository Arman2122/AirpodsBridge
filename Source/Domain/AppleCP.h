// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "Source/Domain/Base.h"

// AppleCP = Apple Continuity Protocols
//
namespace Core::AppleCP {

enum class PacketType : uint8_t {
    AirPrint = 0x3,
    AirDrop = 0x5,
    HomeKit = 0x6,
    ProximityPairing = 0x7,
    HeySiri = 0x8,
    AirPlay = 0x9,
    MagicSwitch = 0xB,
    Handoff = 0xC,
    InstantHotspotTetheringTargetPresence = 0xD,
    InstantHotspotTetheringSourcePresence = 0xE,
    NearbyAction = 0xF,
    NearbyInfo = 0x10,
};

enum class Color : uint8_t {
    White = 0x0,
    Black = 0x1,
    Red = 0x2,
    Blue = 0x3,
    Pink = 0x4,
    Gray = 0x5,
    Silver = 0x6,
    Gold = 0x7,
    RoseGold = 0x8,
    SpaceGray = 0x9,
    DarkBlue = 0xA,
    LightBlue = 0xB,
    Yellow = 0xC,
};

constexpr uint16_t VendorId = 76;

//
// Bounds-checked, strict-aliasing-safe parser for Apple Continuity Proximity Pairing
// advertisements (type 0x07).
//
// Apple BLE manufacturer data (company ID 0x004C) is a sequence of `[type, length, payload]`
// TLV sub-records. A single advertisement may carry Proximity Pairing alone (27 bytes) or
// concatenated alongside other Continuity messages such as NearbyInfo. `Parse` walks the
// TLV stream with explicit bounds checks and extracts fields by byte offset and bitmask
// rather than casting untrusted radio memory to packed structs.
//
class AirPods
{
public:
    static constexpr size_t kPacketSize = 27;
    static constexpr uint8_t kExpectedRemainingLength = 25;
    static constexpr size_t kEncryptedPayloadOffset = 11;

    static bool IsValid(std::span<const uint8_t> data);
    static std::optional<AirPods> Parse(std::span<const uint8_t> data);
    static Core::AirPods::Model GetModel(uint16_t modelId);

    Core::AirPods::Side GetBroadcastedSide() const;
    bool IsLeftBroadcasted() const;
    bool IsRightBroadcasted() const;

    Core::AirPods::Model GetModel() const;

    Core::AirPods::Battery GetLeftBattery() const;
    Core::AirPods::Battery GetRightBattery() const;
    Core::AirPods::Battery GetCaseBattery() const;

    bool IsLeftCharging() const;
    bool IsRightCharging() const;
    bool IsCaseCharging() const;

    bool IsBothPodsInCase() const;
    bool IsLidOpened() const;
    uint8_t GetLidOpenCounter() const;

    // Apple's colour code for the device: the `CL` of its variant in Apple's own asset
    // catalogue, 0 for white AirPods. Wider than the `Color` enum above, which names only
    // the first few.
    uint8_t GetColourCode() const;

    bool IsLeftInEar() const;
    bool IsRightInEar() const;

    AirPods Desensitize() const;
    const std::array<uint8_t, kPacketSize> &RawBytes() const noexcept;

private:
    std::array<uint8_t, kPacketSize> _raw{};

    explicit AirPods(std::array<uint8_t, kPacketSize> raw) noexcept;
};

template <class T>
std::optional<T> As(std::span<const uint8_t> data)
{
    return T::Parse(data);
}

template <class T>
std::optional<T> As(const std::vector<uint8_t> &data)
{
    return T::Parse(std::span<const uint8_t>{data});
}

} // namespace Core::AppleCP
