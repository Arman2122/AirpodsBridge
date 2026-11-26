// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Protocol/UsbIpProtocol.h"

#include <algorithm>
#include <cstring>

namespace Service::Protocol::UsbIp {

uint16_t ReadBe16(std::span<const uint8_t> data, size_t offset)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1]);
}

uint32_t ReadBe32(std::span<const uint8_t> data, size_t offset)
{
    return (static_cast<uint32_t>(data[offset]) << 24) |
           (static_cast<uint32_t>(data[offset + 1]) << 16) |
           (static_cast<uint32_t>(data[offset + 2]) << 8) | static_cast<uint32_t>(data[offset + 3]);
}

void WriteBe16(std::span<uint8_t> data, size_t offset, uint16_t value)
{
    data[offset] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    data[offset + 1] = static_cast<uint8_t>(value & 0xFFu);
}

void WriteBe32(std::span<uint8_t> data, size_t offset, uint32_t value)
{
    data[offset] = static_cast<uint8_t>((value >> 24) & 0xFFu);
    data[offset + 1] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    data[offset + 2] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    data[offset + 3] = static_cast<uint8_t>(value & 0xFFu);
}

int32_t Win32ErrorToErrno(uint32_t win32Error) noexcept
{
    switch (win32Error) {
    case 0:
        return kErrOk;
    case 995: // ERROR_OPERATION_ABORTED
        return kErrConnReset;
    case 31: // ERROR_GEN_FAILURE (USB STALL / pipe error)
        return kErrPipe;
    case 121: // ERROR_SEM_TIMEOUT
        return kErrTimedOut;
    default:
        return kErrProto;
    }
}

std::vector<uint8_t> EnglishLanguageDescriptor()
{
    return {4, 0x03, 0x09, 0x04};
}

std::vector<uint8_t> BuildStringDescriptor(std::string_view utf8)
{
    // Decode UTF-8 code points into UTF-16LE (clamped to 126 code units = 254-byte descriptor).
    std::vector<uint16_t> utf16;
    utf16.reserve(std::min<size_t>(utf8.size(), 126));

    size_t i = 0;
    while (i < utf8.size() && utf16.size() < 126) {
        const uint8_t c = static_cast<uint8_t>(utf8[i]);
        uint32_t cp = 0;
        if (c < 0x80) {
            cp = c;
            i += 1;
        }
        else if ((c & 0xE0) == 0xC0 && i + 1 < utf8.size()) {
            cp = ((c & 0x1Fu) << 6) | (static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu);
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0 && i + 2 < utf8.size()) {
            cp = ((c & 0x0Fu) << 12) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 6) |
                 (static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu);
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0 && i + 3 < utf8.size()) {
            cp = ((c & 0x07u) << 18) | ((static_cast<uint8_t>(utf8[i + 1]) & 0x3Fu) << 12) |
                 ((static_cast<uint8_t>(utf8[i + 2]) & 0x3Fu) << 6) |
                 (static_cast<uint8_t>(utf8[i + 3]) & 0x3Fu);
            i += 4;
        }
        else {
            cp = '?';
            i += 1;
        }

        if (cp <= 0xFFFFu) {
            utf16.push_back(static_cast<uint16_t>(cp));
        }
        else if (utf16.size() + 2 <= 126) {
            cp -= 0x10000u;
            utf16.push_back(static_cast<uint16_t>(0xD800u | (cp >> 10)));
            utf16.push_back(static_cast<uint16_t>(0xDC00u | (cp & 0x3FFu)));
        }
    }

    const size_t totalBytes = 2 + utf16.size() * 2;
    std::vector<uint8_t> desc(totalBytes);
    desc[0] = static_cast<uint8_t>(totalBytes);
    desc[1] = 0x03; // STRING descriptor
    for (size_t idx = 0; idx < utf16.size(); ++idx) {
        desc[2 + idx * 2] = static_cast<uint8_t>(utf16[idx] & 0xFFu);
        desc[2 + idx * 2 + 1] = static_cast<uint8_t>((utf16[idx] >> 8) & 0xFFu);
    }
    return desc;
}

std::vector<InterfaceSummary> ExtractDefaultInterfaces(std::span<const uint8_t> configDescriptor)
{
    std::vector<InterfaceSummary> list;
    size_t pos = 0;
    while (pos + 2 <= configDescriptor.size()) {
        const uint8_t len = configDescriptor[pos];
        const uint8_t type = configDescriptor[pos + 1];
        if (len == 0 || pos + len > configDescriptor.size()) {
            break;
        }
        if (type == 0x04 && len >= 9) { // Interface descriptor
            const uint8_t altSetting = configDescriptor[pos + 3];
            if (altSetting == 0) {
                list.push_back(InterfaceSummary{
                    .interfaceClass = configDescriptor[pos + 5],
                    .interfaceSubClass = configDescriptor[pos + 6],
                    .interfaceProtocol = configDescriptor[pos + 7],
                });
            }
        }
        pos += len;
    }
    return list;
}

BluetoothHciPipes FindBluetoothHciPipes(std::span<const uint8_t> configDescriptor)
{
    BluetoothHciPipes current{};
    bool inDefaultSetting = false;

    size_t pos = 0;
    while (pos + 2 <= configDescriptor.size()) {
        const uint8_t len = configDescriptor[pos];
        const uint8_t type = configDescriptor[pos + 1];
        if (len == 0 || pos + len > configDescriptor.size()) {
            break;
        }

        if (type == 0x04 && len >= 9) { // Interface descriptor
            if (current.Valid()) {
                return current;
            }
            const uint8_t ifaceNum = configDescriptor[pos + 2];
            const uint8_t altSetting = configDescriptor[pos + 3];
            inDefaultSetting = (altSetting == 0);
            current = BluetoothHciPipes{.interfaceNumber = ifaceNum};
        }
        else if (inDefaultSetting && type == 0x05 && len >= 7) { // Endpoint descriptor
            const uint8_t epAddr = configDescriptor[pos + 2];
            const uint8_t epAttrs = static_cast<uint8_t>(configDescriptor[pos + 3] & 0x03u);
            const bool isIn = (epAddr & 0x80u) != 0;

            if (epAttrs == 0x03 && isIn && current.eventPipe == 0) {
                current.eventPipe = epAddr;
            }
            else if (epAttrs == 0x02 && isIn && current.aclInPipe == 0) {
                current.aclInPipe = epAddr;
            }
            else if (epAttrs == 0x02 && !isIn && current.aclOutPipe == 0) {
                current.aclOutPipe = epAddr;
            }
        }

        pos += len;
    }

    return current.Valid() ? current : BluetoothHciPipes{};
}

std::vector<uint8_t> EncodeDeviceRecord(const ExportedDevice &dev, bool includeInterfaces)
{
    const auto ifaces = includeInterfaces ? ExtractDefaultInterfaces(dev.configDescriptor)
                                          : std::vector<InterfaceSummary>{};

    std::vector<uint8_t> buf(kDeviceRecordBytes + ifaces.size() * 4, 0);

    const std::string path = "/sys/devices/platform/airpodsbridge/" + dev.busId;
    std::memcpy(buf.data(), path.data(), std::min<size_t>(path.size(), 255));
    std::memcpy(buf.data() + 256, dev.busId.data(), std::min<size_t>(dev.busId.size(), 31));

    WriteBe32(buf, 288, dev.busNum);
    WriteBe32(buf, 292, dev.devNum);
    WriteBe32(buf, 296, dev.speed);

    if (dev.deviceDescriptor.size() >= 18) {
        // USB descriptors are little-endian; USB/IP header fields are big-endian.
        buf[300] = dev.deviceDescriptor[9];  // idVendor high
        buf[301] = dev.deviceDescriptor[8];  // idVendor low
        buf[302] = dev.deviceDescriptor[11]; // idProduct high
        buf[303] = dev.deviceDescriptor[10]; // idProduct low
        buf[304] = dev.deviceDescriptor[13]; // bcdDevice high
        buf[305] = dev.deviceDescriptor[12]; // bcdDevice low
        buf[306] = dev.deviceDescriptor[4];  // bDeviceClass
        buf[307] = dev.deviceDescriptor[5];  // bDeviceSubClass
        buf[308] = dev.deviceDescriptor[6];  // bDeviceProtocol
        buf[310] = dev.deviceDescriptor[17]; // bNumConfigurations
    }
    if (dev.configDescriptor.size() >= 6) {
        buf[309] = dev.configDescriptor[5]; // bConfigurationValue
        buf[311] = dev.configDescriptor[4]; // bNumInterfaces
    }

    if (includeInterfaces) {
        size_t off = kDeviceRecordBytes;
        for (const auto &iface : ifaces) {
            buf[off] = iface.interfaceClass;
            buf[off + 1] = iface.interfaceSubClass;
            buf[off + 2] = iface.interfaceProtocol;
            buf[off + 3] = 0; // padding
            off += 4;
        }
    }

    return buf;
}

std::vector<uint8_t> EncodeOpRepDevList(std::span<const ExportedDevice> devices)
{
    std::vector<uint8_t> out(12, 0);
    WriteBe16(out, 0, kVersion);
    WriteBe16(out, 2, kOpRepDevList);
    WriteBe32(out, 4, 0); // status OK
    WriteBe32(out, 8, static_cast<uint32_t>(devices.size()));
    for (const auto &dev : devices) {
        const auto rec = EncodeDeviceRecord(dev, true);
        out.insert(out.end(), rec.begin(), rec.end());
    }
    return out;
}

std::vector<uint8_t> EncodeOpRepImport(const ExportedDevice *device, uint32_t status)
{
    std::vector<uint8_t> out(8, 0);
    WriteBe16(out, 0, kVersion);
    WriteBe16(out, 2, kOpRepImport);
    WriteBe32(out, 4, status);
    if (status == 0 && device != nullptr) {
        const auto rec = EncodeDeviceRecord(*device, false);
        out.insert(out.end(), rec.begin(), rec.end());
    }
    return out;
}

std::optional<CommandHeader> DecodeCommandHeader(std::span<const uint8_t> header48)
{
    if (header48.size() < kUrbHeaderBytes) {
        return std::nullopt;
    }

    CommandHeader cmd{};
    cmd.command = ReadBe32(header48, 0);
    cmd.seqnum = ReadBe32(header48, 4);
    cmd.devid = ReadBe32(header48, 8);
    cmd.direction = ReadBe32(header48, 12);
    cmd.endpoint = ReadBe32(header48, 16);

    if (cmd.command == kCmdSubmit) {
        cmd.transferFlags = ReadBe32(header48, 20);
        cmd.transferBufferLength = ReadBe32(header48, 24);
        cmd.startFrame = ReadBe32(header48, 28);
        cmd.numberOfPackets = ReadBe32(header48, 32);
        cmd.interval = ReadBe32(header48, 36);
        std::copy_n(header48.begin() + 40, 8, cmd.setup.begin());
        return cmd;
    }
    if (cmd.command == kCmdUnlink) {
        cmd.unlinkSeqnum = ReadBe32(header48, 20);
        return cmd;
    }
    return std::nullopt;
}

std::vector<uint8_t> EncodeRetSubmit(
    uint32_t seqnum, int32_t status, uint32_t actualLength, std::span<const uint8_t> data)
{
    std::vector<uint8_t> out(kUrbHeaderBytes + data.size(), 0);
    WriteBe32(out, 0, kRetSubmit);
    WriteBe32(out, 4, seqnum);
    WriteBe32(out, 20, static_cast<uint32_t>(status));
    WriteBe32(out, 24, actualLength);
    WriteBe32(out, 32, kNoIsoPackets);
    if (!data.empty()) {
        std::memcpy(out.data() + kUrbHeaderBytes, data.data(), data.size());
    }
    return out;
}

std::vector<uint8_t> EncodeRetUnlink(uint32_t seqnum, int32_t status)
{
    std::vector<uint8_t> out(kUrbHeaderBytes, 0);
    WriteBe32(out, 0, kRetUnlink);
    WriteBe32(out, 4, seqnum);
    WriteBe32(out, 20, static_cast<uint32_t>(status));
    return out;
}

std::vector<uint8_t> EncodeRetIso(
    uint32_t seqnum, int32_t status, uint32_t startFrame,
    std::span<const uint8_t> cmdIsoDescriptors, uint32_t packetCount, bool isIn,
    const IsoTransferResult *result)
{
    std::vector<uint8_t> payload;
    std::vector<uint8_t> descriptors(static_cast<size_t>(packetCount) * kIsoDescriptorBytes, 0);

    uint32_t totalActual = 0;
    uint32_t errorCount = 0;
    size_t srcIdx = 0;

    for (uint32_t i = 0; i < packetCount; ++i) {
        const size_t descOff = static_cast<size_t>(i) * kIsoDescriptorBytes;
        const uint32_t cmdOffset =
            descOff + 16 <= cmdIsoDescriptors.size() ? ReadBe32(cmdIsoDescriptors, descOff) : 0;
        const uint32_t cmdLength =
            descOff + 16 <= cmdIsoDescriptors.size() ? ReadBe32(cmdIsoDescriptors, descOff + 4) : 0;

        uint32_t actual = 0;
        int32_t pktStatus = kErrExdev;

        if (result != nullptr && isIn) {
            // Front-pack non-empty or failed IN packets into the earliest descriptors in order.
            // usbip-win2 places packed payload bytes at the start of the URB buffer and scatters
            // them to each descriptor's offset; packing front-first prevents stale bytes at
            // offset 0 from being interpreted as duplicate SCO packets by bthusb.
            pktStatus = kErrOk;
            while (srcIdx < result->packets.size() && result->packets[srcIdx].actualLength == 0 &&
                   result->packets[srcIdx].status == 0)
            {
                ++srcIdx;
            }
            if (srcIdx < result->packets.size()) {
                const auto &pkt = result->packets[srcIdx++];
                pktStatus = pkt.status;
                if (pkt.actualLength > 0 && pkt.offset < result->data.size()) {
                    const uint32_t avail = static_cast<uint32_t>(result->data.size() - pkt.offset);
                    actual = std::min({pkt.actualLength, cmdLength, avail});
                    if (actual > 0) {
                        payload.insert(
                            payload.end(),
                            result->data.begin() + static_cast<std::ptrdiff_t>(pkt.offset),
                            result->data.begin() +
                                static_cast<std::ptrdiff_t>(pkt.offset + actual));
                    }
                }
            }
        }
        else if (result != nullptr && !isIn && i < result->packets.size()) {
            actual = result->packets[i].actualLength;
            pktStatus = result->packets[i].status;
        }
        else if (status == 0 && !isIn) {
            actual = cmdLength;
            pktStatus = kErrOk;
        }

        if (pktStatus != 0) {
            ++errorCount;
        }
        totalActual += actual;

        WriteBe32(descriptors, descOff, cmdOffset);
        WriteBe32(descriptors, descOff + 4, cmdLength);
        WriteBe32(descriptors, descOff + 8, actual);
        WriteBe32(descriptors, descOff + 12, static_cast<uint32_t>(pktStatus));
    }

    std::vector<uint8_t> out(kUrbHeaderBytes + payload.size() + descriptors.size(), 0);
    WriteBe32(out, 0, kRetSubmit);
    WriteBe32(out, 4, seqnum);
    WriteBe32(out, 20, static_cast<uint32_t>(status));
    WriteBe32(out, 24, totalActual);
    WriteBe32(out, 28, startFrame);
    WriteBe32(out, 32, packetCount);
    WriteBe32(out, 36, errorCount);

    if (!payload.empty()) {
        std::memcpy(out.data() + kUrbHeaderBytes, payload.data(), payload.size());
    }
    if (!descriptors.empty()) {
        std::memcpy(
            out.data() + kUrbHeaderBytes + payload.size(), descriptors.data(), descriptors.size());
    }
    return out;
}

} // namespace Service::Protocol::UsbIp
