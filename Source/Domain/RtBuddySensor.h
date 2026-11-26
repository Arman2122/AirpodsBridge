// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Core::AirPods::RtBuddy {

//
// Apple RTBuddy sensor protocol (AAP opcode 0x17, descriptor 0x00100000 `SensorDataWX`)
// used by AirPods Pro 3 for optical PPG heart-rate telemetry and dynamic HID service
// discovery. Pure domain codec with zero platform dependencies.
//

inline constexpr uint8_t kLegacyHeartRateService = 19;        // 0x13 (HEARTRATE)
inline constexpr uint8_t kLegacyHeartRateV2Service = 20;      // 0x14 (HEARTRATEv2)
inline constexpr uint8_t kLegacyHeartRateCommandService = 84; // 0x54 (HEARTRATE_COMMAND)
inline constexpr uint32_t kDefaultReportPeriodUs = 1000000u;  // 1 Hz optical stream

enum class DescriptorKind : uint8_t {
    Motion = 0,
    HeartRate = 1,
};

struct HeartRateSample {
    uint16_t bpm{0};
    uint8_t confidence{0}; // 0..255 raw optical lock quality (>= 0x80 locked)

    bool operator==(const HeartRateSample &) const = default;
};

struct SensorBatch {
    bool mappingChanged{false};
    bool warmupFrameSeen{false};
    std::optional<HeartRateSample> heartRate;

    bool operator==(const SensorBatch &) const = default;
};

// Outbound RTBuddy / HRM session control packets.
std::vector<uint8_t> BuildConnectService0Packet();
std::vector<uint8_t> BuildCapabilitiesService0Packet();
std::vector<uint8_t> BuildConnectService4Packet();
std::vector<uint8_t> BuildCapabilitiesService4Packet();
std::vector<uint8_t> BuildHrmStatePacket(bool enabled);
std::vector<uint8_t> BuildRequestAllDescriptorsPacket(uint8_t sequence);
std::vector<uint8_t>
BuildServiceReportIntervalPacket(uint8_t sequence, uint8_t service, uint32_t periodUs);

// Parses an Apple binary HID sensor descriptor dictionary (`0xD3 0x00 0x00 0x00`, VendorID 1452).
std::optional<DescriptorKind> ParseSensorDescriptorKind(std::span<const uint8_t> dict);

// Reassembles RTBuddy `0x17` SensorDataWX frames that may be split or coalesced across reads.
class FrameReassembler
{
public:
    std::vector<std::vector<uint8_t>> Feed(std::span<const uint8_t> chunk);
    void Reset();

private:
    std::vector<uint8_t> _carry;
};

// Stateful per-connection RTBuddy `SensorDataWX` parser. Learns dynamically announced sensor
// service numbers from HID descriptor dictionaries and validates optical heart-rate reports.
class SessionParser
{
public:
    void Reset();

    SensorBatch ParseFrame(std::span<const uint8_t> frame);

    std::optional<uint8_t> HeartRateService() const noexcept
    {
        return _heartRateService;
    }
    std::optional<uint8_t> MotionService() const noexcept
    {
        return _motionService;
    }

    // Services to configure when starting or stopping heart-rate streaming.
    std::vector<uint8_t> TargetHeartRateServices() const;

private:
    std::optional<uint8_t> _motionService;
    std::optional<uint8_t> _heartRateService;
    bool _heartDescriptorSeen{false};
    std::optional<uint64_t> _lastDescriptorSequence;
};

// Stateless single-frame helper (uses legacy service IDs 19/20/84).
std::optional<HeartRateSample> TryDecodeHeartRatePacket(std::span<const uint8_t> frame);

} // namespace Core::AirPods::RtBuddy
