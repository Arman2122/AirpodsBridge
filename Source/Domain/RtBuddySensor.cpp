// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/RtBuddySensor.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Core::AirPods::RtBuddy {

namespace {

constexpr std::array<uint8_t, 6> kRtBuddyPrefix{0x04, 0x00, 0x04, 0x00, 0x17, 0x00};
constexpr std::array<uint8_t, 4> kSensorDataWxDescriptor{0x00, 0x00, 0x10, 0x00};
constexpr size_t kFrameHeaderBytes = 12;
constexpr size_t kMaxRecordBytes = 16384;
constexpr size_t kMaxProtoFields = 96;
constexpr uint64_t kMaxProtoFieldNumber = 4096;
constexpr int kMaxProtoDepth = 3;

// Exact 96-byte Apple motion HID descriptor.
constexpr std::array<uint8_t, 96> kExactMotionDescriptor{
    0x06, 0x0c, 0xff, 0x09, 0x11, 0xa1, 0x01, 0x06, 0x00, 0xff, 0x09, 0x23, 0xa1, 0x00, 0x85, 0x01,
    0x06, 0x15, 0xff, 0x09, 0x04, 0x95, 0x01, 0x75, 0x40, 0x81, 0x02, 0x06, 0x0c, 0xff, 0x09, 0x0d,
    0x76, 0xb0, 0x05, 0x81, 0x02, 0x85, 0x02, 0x09, 0x0e, 0x81, 0x02, 0xc0, 0x85, 0x01, 0x05, 0x20,
    0x0a, 0x0e, 0x03, 0x75, 0x20, 0xb1, 0x02, 0x85, 0x02, 0x0a, 0x1b, 0x03, 0xb1, 0x02, 0x06, 0x15,
    0xff, 0x0a, 0x01, 0x00, 0x85, 0x03, 0xb1, 0x02, 0x85, 0x04, 0x05, 0x20, 0x0a, 0x1a, 0x03, 0xb1,
    0x02, 0x85, 0xff, 0x06, 0x15, 0xff, 0x0a, 0x20, 0x00, 0x75, 0x08, 0x95, 0xe0, 0xb1, 0x02, 0xc0,
};

// Exact 126-byte Apple AirPods Pro 3 HeartRate HID descriptor.
constexpr std::array<uint8_t, 126> kExactHeartRateHidDescriptor{
    0x05, 0x20, 0x09, 0x16, 0xa1, 0x01, 0x85, 0x01, 0x0a, 0x0e, 0x03, 0x14, 0x27, 0xff, 0xff, 0xff,
    0x7f, 0x75, 0x20, 0x95, 0x01, 0xb1, 0x02, 0x0a, 0xb8, 0x04, 0x26, 0xff, 0x00, 0x75, 0x08, 0x95,
    0x01, 0x81, 0x02, 0x06, 0x15, 0xff, 0x0a, 0x20, 0x01, 0x95, 0x01, 0x75, 0x08, 0x81, 0x02, 0x26,
    0xff, 0x7f, 0x06, 0x15, 0xff, 0x0a, 0x21, 0x01, 0x95, 0x01, 0x75, 0x10, 0x81, 0x02, 0x75, 0x08,
    0x95, 0x01, 0x15, 0x01, 0x25, 0x02, 0x1a, 0x04, 0x01, 0x2a, 0x05, 0x01, 0x81, 0x00, 0x06, 0x00,
    0xff, 0x09, 0x23, 0xa1, 0x00, 0x15, 0x00, 0x24, 0x06, 0x15, 0xff, 0x09, 0x04, 0x95, 0x08, 0x75,
    0x08, 0x81, 0x02, 0x06, 0x0a, 0xff, 0x09, 0x12, 0x95, 0x04, 0x75, 0x08, 0x81, 0x02, 0x85, 0x02,
    0x06, 0x0a, 0xff, 0x09, 0x13, 0x96, 0x58, 0x02, 0x75, 0x08, 0x81, 0x02, 0xc0, 0xc0,
};

struct ProtoField {
    uint32_t number{0};
    uint8_t wire{0};
    uint64_t scalar{0};
    std::span<const uint8_t> bytes;
};

bool ReadVarint(std::span<const uint8_t> data, size_t &pos, uint64_t &out)
{
    out = 0;
    for (int shift = 0; pos < data.size() && shift < 64; shift += 7) {
        const uint8_t current = data[pos++];
        if (shift == 63 && current > 1) {
            return false;
        }
        out |= static_cast<uint64_t>(current & 0x7Fu) << shift;
        if ((current & 0x80u) == 0) {
            return true;
        }
    }
    return false;
}

bool ReadField(std::span<const uint8_t> data, size_t &pos, ProtoField &field)
{
    field = {};
    uint64_t key = 0;
    if (pos >= data.size() || !ReadVarint(data, pos, key)) {
        return false;
    }
    const uint64_t num = key >> 3;
    const uint8_t wire = static_cast<uint8_t>(key & 7u);
    if (num == 0 || num > kMaxProtoFieldNumber) {
        return false;
    }
    field.number = static_cast<uint32_t>(num);
    field.wire = wire;
    switch (wire) {
    case 0:
        return ReadVarint(data, pos, field.scalar);
    case 2: {
        uint64_t len = 0;
        if (!ReadVarint(data, pos, len) || len > data.size() - pos) {
            return false;
        }
        field.bytes = data.subspan(pos, static_cast<size_t>(len));
        pos += static_cast<size_t>(len);
        return true;
    }
    case 1:
    case 5: {
        const size_t size = (wire == 1) ? 8 : 4;
        if (data.size() - pos < size) {
            return false;
        }
        field.bytes = data.subspan(pos, size);
        pos += size;
        return true;
    }
    default:
        return false;
    }
}

bool IsValidProtoMessage(std::span<const uint8_t> data)
{
    if (data.size() > kMaxRecordBytes) {
        return false;
    }
    size_t pos = 0;
    size_t fields = 0;
    ProtoField ignored;
    while (pos < data.size()) {
        if (fields++ >= kMaxProtoFields || !ReadField(data, pos, ignored)) {
            return false;
        }
    }
    return true;
}

bool IsValidHeartRateTail(uint8_t a, uint8_t b, uint8_t c)
{
    return (a == 0x10 && b == 0x00 && (c == 0x00 || c == 0x80)) ||
           (a == 0x20 && b == 0x00 && c == 0x00) || (a == 0x20 && b == 0x80 && c == 0x00) ||
           (a == 0x20 && b == 0x02 && c == 0x80) || (a == 0x20 && b == 0x82 && c == 0x80);
}

enum class ReadingMatch : uint8_t {
    None = 0,
    Warmup = 1,
    Locked = 2,
};

ReadingMatch InspectReadingPayload(std::span<const uint8_t> payload, HeartRateSample &out)
{
    if (payload.size() != 18) {
        return ReadingMatch::None;
    }
    const uint8_t bpm = payload[1];
    const uint8_t confidence = payload[2];
    if (bpm >= 30 && bpm <= 220 && confidence >= 0x80 &&
        IsValidHeartRateTail(payload[15], payload[16], payload[17]))
    {
        out = HeartRateSample{.bpm = bpm, .confidence = confidence};
        return ReadingMatch::Locked;
    }
    // An 18-byte report from the heart-rate service that has not reached optical lock yet
    // (or carries a warm-up tail) proves the PPG optical sensor is actively warming up.
    return ReadingMatch::Warmup;
}

ReadingMatch FindPayloadRecursive(std::span<const uint8_t> data, int depth, HeartRateSample &out)
{
    if (data.size() == 18) {
        return InspectReadingPayload(data, out);
    }
    if (depth >= kMaxProtoDepth || !IsValidProtoMessage(data)) {
        return ReadingMatch::None;
    }
    ReadingMatch best = ReadingMatch::None;
    size_t pos = 0;
    ProtoField f;
    while (ReadField(data, pos, f)) {
        if (f.wire == 2) {
            const ReadingMatch m = FindPayloadRecursive(f.bytes, depth + 1, out);
            if (m == ReadingMatch::Locked) {
                return ReadingMatch::Locked;
            }
            if (m == ReadingMatch::Warmup) {
                best = ReadingMatch::Warmup;
            }
        }
    }
    return best;
}

bool IsAcceptedHeartRateService(
    uint64_t service, std::optional<uint8_t> announcedHr, std::optional<uint8_t> announcedMotion,
    bool heartDescriptorSeen)
{
    if (service == 0 || service > 127) {
        return false;
    }
    const uint8_t svc = static_cast<uint8_t>(service);
    if (announcedHr.has_value()) {
        return svc == announcedHr.value();
    }
    if (heartDescriptorSeen) {
        return false;
    }
    if (announcedMotion.has_value() && svc == announcedMotion.value()) {
        return false;
    }
    return svc == kLegacyHeartRateService || svc == kLegacyHeartRateV2Service ||
           svc == kLegacyHeartRateCommandService;
}

ReadingMatch FindCommandRecursive(
    std::span<const uint8_t> data, int depth, std::optional<uint8_t> announcedHr,
    std::optional<uint8_t> announcedMotion, bool heartDescriptorSeen, HeartRateSample &out)
{
    if (depth > kMaxProtoDepth || !IsValidProtoMessage(data)) {
        return ReadingMatch::None;
    }

    ReadingMatch best = ReadingMatch::None;
    size_t pos = 0;
    ProtoField f;
    while (ReadField(data, pos, f)) {
        if (f.number == 1 && f.wire == 0) {
            if (IsAcceptedHeartRateService(
                    f.scalar, announcedHr, announcedMotion, heartDescriptorSeen))
            {
                size_t inner = 0;
                ProtoField pf;
                while (ReadField(data, inner, pf)) {
                    if (pf.number == 3 && pf.wire == 2) {
                        const ReadingMatch m = FindPayloadRecursive(pf.bytes, 0, out);
                        if (m == ReadingMatch::Locked) {
                            return ReadingMatch::Locked;
                        }
                        if (m == ReadingMatch::Warmup) {
                            best = ReadingMatch::Warmup;
                        }
                    }
                }
            }
            break;
        }
    }

    pos = 0;
    while (ReadField(data, pos, f)) {
        if (f.wire == 2) {
            const ReadingMatch m = FindCommandRecursive(
                f.bytes, depth + 1, announcedHr, announcedMotion, heartDescriptorSeen, out);
            if (m == ReadingMatch::Locked) {
                return ReadingMatch::Locked;
            }
            if (m == ReadingMatch::Warmup) {
                best = ReadingMatch::Warmup;
            }
        }
    }
    return best;
}

std::string TrimTrailingNuls(std::span<const uint8_t> bytes)
{
    size_t len = bytes.size();
    while (len > 0 && bytes[len - 1] == 0) {
        --len;
    }
    return std::string{reinterpret_cast<const char *>(bytes.data()), len};
}

} // namespace

std::vector<uint8_t> BuildConnectService0Packet()
{
    return {
        0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
}

std::vector<uint8_t> BuildCapabilitiesService0Packet()
{
    return {0x04, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
}

std::vector<uint8_t> BuildConnectService4Packet()
{
    return {
        0x00, 0x00, 0x04, 0x00, 0x01, 0x00, 0x03, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
}

std::vector<uint8_t> BuildCapabilitiesService4Packet()
{
    return {0x04, 0x00, 0x04, 0x00, 0x01, 0x00, 0x00};
}

std::vector<uint8_t> BuildHrmStatePacket(bool enabled)
{
    return {
        0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x30, static_cast<uint8_t>(enabled ? 0x01 : 0x02),
        0x00, 0x00, 0x00,
    };
}

std::vector<uint8_t> BuildRequestAllDescriptorsPacket(uint8_t sequence)
{
    const uint8_t seq = static_cast<uint8_t>(std::max<uint8_t>(1, sequence & 0x7Fu));
    return {
        0x04, 0x00, 0x04, 0x00, 0x17, 0x00, 0x00, 0x00,
        0x10, 0x00, 0x04, 0x00, 0x08, seq,  0x22, 0x00,
    };
}

std::vector<uint8_t>
BuildServiceReportIntervalPacket(uint8_t sequence, uint8_t service, uint32_t periodUs)
{
    const uint8_t seq = static_cast<uint8_t>(std::max<uint8_t>(1, sequence & 0x7Fu));
    const uint8_t svc = static_cast<uint8_t>(service & 0x7Fu);
    return {
        0x04,
        0x00,
        0x04,
        0x00,
        0x17,
        0x00,
        0x00,
        0x00,
        0x10,
        0x00,
        0x11,
        0x00,
        0x08,
        seq,
        0x10,
        0x02,
        0x42,
        0x0B,
        0x08,
        svc,
        0x10,
        0x02,
        0x1A,
        0x05,
        0x01,
        static_cast<uint8_t>(periodUs & 0xFFu),
        static_cast<uint8_t>((periodUs >> 8) & 0xFFu),
        static_cast<uint8_t>((periodUs >> 16) & 0xFFu),
        static_cast<uint8_t>((periodUs >> 24) & 0xFFu),
    };
}

std::optional<DescriptorKind> ParseSensorDescriptorKind(std::span<const uint8_t> dict)
{
    if (dict.size() < 8 || dict.size() > kMaxRecordBytes || dict[0] != 0xD3 || dict[1] != 0x00 ||
        dict[2] != 0x00 || dict[3] != 0x00)
    {
        return std::nullopt;
    }

    size_t offset = 4;
    auto take = [&](size_t size, std::span<const uint8_t> &out) -> bool {
        if (size > dict.size() - offset) {
            return false;
        }
        out = dict.subspan(offset, size);
        const size_t padded = (offset + size + 3u) & ~size_t{3};
        if (padded > dict.size()) {
            return false;
        }
        offset = padded;
        return true;
    };

    auto readWord = [&](uint32_t &out) -> bool {
        std::span<const uint8_t> bytes;
        if (!take(4, bytes)) {
            return false;
        }
        out = static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8) |
              (static_cast<uint32_t>(bytes[2]) << 16) | (static_cast<uint32_t>(bytes[3]) << 24);
        return true;
    };

    struct DictItem {
        uint8_t kind{0};
        std::span<const uint8_t> bytes;
    };

    auto readItem = [&](DictItem &out) -> bool {
        uint32_t header = 0;
        if (!readWord(header)) {
            return false;
        }
        const uint8_t kind = static_cast<uint8_t>((header >> 24) & 0x7Fu);
        const uint32_t count = header & 0x00FFFFFFu;
        size_t len = 0;
        if (kind == 4 && count <= 64) {
            len = 8;
        }
        else if (kind >= 8 && kind <= 10) {
            len = count;
        }
        else if (kind == 11 && count <= 1) {
            len = 0;
        }
        else {
            return false;
        }
        out.kind = kind;
        return take(len, out.bytes);
    };

    uint32_t rootHeader = 0;
    if (!readWord(rootHeader)) {
        return std::nullopt;
    }
    const uint32_t count = rootHeader & 0x00FFFFFFu;
    if ((rootHeader & 0x7F000000u) != 0x01000000u || count > 96) {
        return std::nullopt;
    }

    std::unordered_map<std::string, DictItem> values;
    values.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        DictItem keyItem;
        if (!readItem(keyItem) || keyItem.kind < 8 || keyItem.kind > 9 ||
            keyItem.bytes.size() > 128)
        {
            return std::nullopt;
        }
        std::string key = TrimTrailingNuls(keyItem.bytes);
        if (values.contains(key)) {
            return std::nullopt;
        }
        DictItem valItem;
        if (!readItem(valItem)) {
            return std::nullopt;
        }
        values.emplace(std::move(key), valItem);
    }

    if (offset != dict.size()) {
        return std::nullopt;
    }

    auto readNumber = [&](const char *key) -> std::optional<uint64_t> {
        const auto it = values.find(key);
        if (it == values.end() || it->second.kind != 4 || it->second.bytes.size() != 8) {
            return std::nullopt;
        }
        uint64_t value = 0;
        for (size_t i = 0; i < 8; ++i) {
            value |= static_cast<uint64_t>(it->second.bytes[i]) << (i * 8);
        }
        return value;
    };

    if (readNumber("VendorID") != std::optional<uint64_t>{1452}) {
        return std::nullopt;
    }

    const auto accIt = values.find("AccessoryService");
    const auto hrIt = values.find("HeartRateService");
    const bool hasAcc = accIt != values.end();
    const bool hasHr = hrIt != values.end();
    if (hasAcc == hasHr) {
        return std::nullopt;
    }
    const auto &nameItem = hasAcc ? accIt->second : hrIt->second;
    if (nameItem.kind < 8 || nameItem.kind > 9) {
        return std::nullopt;
    }
    const std::string serviceName = TrimTrailingNuls(nameItem.bytes);

    const auto descIt = values.find("ReportDescriptor");
    if (descIt == values.end() || descIt->second.kind != 10) {
        return std::nullopt;
    }
    const auto descBytes = descIt->second.bytes;
    const auto maxReportSize = readNumber("MaxReportSize");

    if (serviceName == "devmotion6" && maxReportSize == std::optional<uint64_t>{191} &&
        descBytes.size() == kExactMotionDescriptor.size() &&
        std::equal(descBytes.begin(), descBytes.end(), kExactMotionDescriptor.begin()))
    {
        return DescriptorKind::Motion;
    }
    if (serviceName == "HeartRate" && maxReportSize == std::optional<uint64_t>{601} &&
        descBytes.size() == kExactHeartRateHidDescriptor.size() &&
        std::equal(descBytes.begin(), descBytes.end(), kExactHeartRateHidDescriptor.begin()))
    {
        return DescriptorKind::HeartRate;
    }
    return std::nullopt;
}

std::vector<std::vector<uint8_t>> FrameReassembler::Feed(std::span<const uint8_t> chunk)
{
    if (chunk.size() > kMaxRecordBytes || _carry.size() + chunk.size() > 2 * kMaxRecordBytes) {
        _carry.clear();
        return {};
    }

    std::vector<uint8_t> input;
    input.reserve(_carry.size() + chunk.size());
    input.insert(input.end(), _carry.begin(), _carry.end());
    input.insert(input.end(), chunk.begin(), chunk.end());

    std::vector<std::vector<uint8_t>> frames;
    size_t cursor = 0;
    while (input.size() - cursor >= kRtBuddyPrefix.size()) {
        if (!std::equal(
                kRtBuddyPrefix.begin(), kRtBuddyPrefix.end(),
                input.begin() + static_cast<std::ptrdiff_t>(cursor)))
        {
            ++cursor;
            continue;
        }
        if (input.size() - cursor < kFrameHeaderBytes) {
            break;
        }
        const uint16_t length =
            static_cast<uint16_t>(input[cursor + 10]) |
            static_cast<uint16_t>(static_cast<uint16_t>(input[cursor + 11]) << 8);
        if (length > kMaxRecordBytes) {
            cursor += kRtBuddyPrefix.size();
            continue;
        }
        const size_t end = cursor + kFrameHeaderBytes + length;
        if (end > input.size()) {
            break;
        }
        if (std::equal(
                kSensorDataWxDescriptor.begin(), kSensorDataWxDescriptor.end(),
                input.begin() + static_cast<std::ptrdiff_t>(cursor + 6)))
        {
            frames.emplace_back(
                input.begin() + static_cast<std::ptrdiff_t>(cursor),
                input.begin() + static_cast<std::ptrdiff_t>(end));
        }
        cursor = end;
    }

    _carry.assign(input.begin() + static_cast<std::ptrdiff_t>(cursor), input.end());
    return frames;
}

void FrameReassembler::Reset()
{
    _carry.clear();
}

void SessionParser::Reset()
{
    _motionService.reset();
    _heartRateService.reset();
    _heartDescriptorSeen = false;
    _lastDescriptorSequence.reset();
}

std::vector<uint8_t> SessionParser::TargetHeartRateServices() const
{
    if (_heartRateService.has_value()) {
        return {_heartRateService.value()};
    }
    return {kLegacyHeartRateV2Service, kLegacyHeartRateCommandService, kLegacyHeartRateService};
}

SensorBatch SessionParser::ParseFrame(std::span<const uint8_t> frame)
{
    SensorBatch result;
    if (frame.size() < kFrameHeaderBytes ||
        !std::equal(kRtBuddyPrefix.begin(), kRtBuddyPrefix.end(), frame.begin()) ||
        !std::equal(
            kSensorDataWxDescriptor.begin(), kSensorDataWxDescriptor.end(), frame.begin() + 6))
    {
        return result;
    }

    const size_t payloadLen =
        static_cast<size_t>(frame[10]) | (static_cast<size_t>(frame[11]) << 8);
    if (payloadLen > frame.size() - kFrameHeaderBytes) {
        return result;
    }
    const auto message = frame.subspan(kFrameHeaderBytes, payloadLen);
    if (!IsValidProtoMessage(message)) {
        return result;
    }

    uint64_t seq = 0;
    uint64_t logType = 0;
    std::vector<std::span<const uint8_t>> descriptors;
    std::vector<std::span<const uint8_t>> nestedMessages;

    size_t pos = 0;
    ProtoField f;
    while (ReadField(message, pos, f)) {
        if (f.number == 1 && f.wire == 0) {
            seq = f.scalar;
        }
        else if (f.number == 2 && f.wire == 0) {
            logType = f.scalar;
        }
        else if (f.number == 5 && f.wire == 2) {
            descriptors.push_back(f.bytes);
        }
        else if (f.wire == 2) {
            nestedMessages.push_back(f.bytes);
        }
    }

    if (logType != 1 && logType != 3) {
        return result;
    }

    // 1. Discover dynamic sensor service numbers from repeated SensorDescriptor announcements.
    if (!descriptors.empty() && descriptors.size() <= 96) {
        std::vector<std::pair<uint8_t, DescriptorKind>> announced;
        bool descriptorsValid = true;
        for (const auto &descMsg : descriptors) {
            if (!IsValidProtoMessage(descMsg)) {
                descriptorsValid = false;
                break;
            }
            std::optional<uint8_t> svc;
            std::span<const uint8_t> rawDict;
            size_t dpos = 0;
            ProtoField df;
            while (ReadField(descMsg, dpos, df)) {
                if (df.number == 1 && df.wire == 0 && df.scalar >= 1 && df.scalar <= 127) {
                    svc = static_cast<uint8_t>(df.scalar);
                }
                else if (df.number == 2 && df.wire == 2) {
                    rawDict = df.bytes;
                }
            }
            if (!svc.has_value()) {
                descriptorsValid = false;
                break;
            }
            if (const auto kind = ParseSensorDescriptorKind(rawDict); kind.has_value()) {
                announced.emplace_back(svc.value(), kind.value());
            }
        }

        if (descriptorsValid) {
            bool conflict = false;
            for (size_t i = 0; i < announced.size() && !conflict; ++i) {
                for (size_t j = i + 1; j < announced.size(); ++j) {
                    const bool sameSvc = announced[i].first == announced[j].first;
                    const bool sameKind = announced[i].second == announced[j].second;
                    if (sameSvc != sameKind) {
                        conflict = true;
                        break;
                    }
                }
            }

            if (!conflict) {
                const bool restart = seq == 1 && _lastDescriptorSequence.value_or(0) > 1;
                auto nextMotion = restart ? std::optional<uint8_t>{} : _motionService;
                auto nextHeart = restart ? std::optional<uint8_t>{} : _heartRateService;
                for (const auto &[svc, kind] : announced) {
                    if (kind == DescriptorKind::Motion) {
                        if (nextHeart.has_value() && nextHeart.value() == svc) {
                            conflict = true;
                            break;
                        }
                        nextMotion = svc;
                    }
                    else if (kind == DescriptorKind::HeartRate) {
                        if (nextMotion.has_value() && nextMotion.value() == svc) {
                            conflict = true;
                            break;
                        }
                        nextHeart = svc;
                    }
                }
                if (!conflict) {
                    const bool changed =
                        restart || nextMotion != _motionService || nextHeart != _heartRateService;
                    _motionService = nextMotion;
                    _heartRateService = nextHeart;
                    _heartDescriptorSeen = _heartDescriptorSeen || nextHeart.has_value();
                    _lastDescriptorSequence = seq;
                    result.mappingChanged = changed;
                }
            }
        }
    }

    // 2. Decode heart-rate reports from SensorCommand (field 7) or nested command containers.
    HeartRateSample sample;
    for (const auto &sub : nestedMessages) {
        const ReadingMatch match = FindCommandRecursive(
            sub, 0, _heartRateService, _motionService, _heartDescriptorSeen, sample);
        if (match == ReadingMatch::Locked) {
            result.heartRate = sample;
            result.warmupFrameSeen = false;
            break;
        }
        if (match == ReadingMatch::Warmup) {
            result.warmupFrameSeen = true;
        }
    }

    return result;
}

std::optional<HeartRateSample> TryDecodeHeartRatePacket(std::span<const uint8_t> frame)
{
    SessionParser parser;
    return parser.ParseFrame(frame).heartRate;
}

} // namespace Core::AirPods::RtBuddy
