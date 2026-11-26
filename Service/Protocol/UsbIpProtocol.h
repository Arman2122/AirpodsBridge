// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Service::Protocol::UsbIp {

//
// USB/IP v1.1.1 wire protocol (big-endian network byte order) compatible with Microsoft-
// signed `usbip-win2` (`usbip2_ude`).
//
// Zero platform headers: unit-tested directly against raw captures and synthetic URBs.
//

inline constexpr uint16_t kVersion = 0x0111;

inline constexpr uint16_t kOpReqDevList = 0x8005;
inline constexpr uint16_t kOpRepDevList = 0x0005;
inline constexpr uint16_t kOpReqImport = 0x8003;
inline constexpr uint16_t kOpRepImport = 0x0003;

inline constexpr uint32_t kCmdSubmit = 0x00000001;
inline constexpr uint32_t kCmdUnlink = 0x00000002;
inline constexpr uint32_t kRetSubmit = 0x00000003;
inline constexpr uint32_t kRetUnlink = 0x00000004;

inline constexpr uint32_t kDirOut = 0;
inline constexpr uint32_t kDirIn = 1;

inline constexpr uint32_t kNoIsoPackets = 0xFFFFFFFFu;
inline constexpr size_t kUrbHeaderBytes = 48;
inline constexpr size_t kDeviceRecordBytes = 312;
inline constexpr size_t kIsoDescriptorBytes = 16;

// USB speed codes defined by Linux usbip / usbip-win2.
inline constexpr uint32_t kSpeedUnknown = 0;
inline constexpr uint32_t kSpeedLow = 1;
inline constexpr uint32_t kSpeedFull = 2;
inline constexpr uint32_t kSpeedHigh = 3;
inline constexpr uint32_t kSpeedSuper = 5;

// Linux errno values expected in USB/IP RET_SUBMIT / RET_UNLINK status fields.
inline constexpr int32_t kErrOk = 0;
inline constexpr int32_t kErrExdev = -18;
inline constexpr int32_t kErrPipe = -32;
inline constexpr int32_t kErrProto = -71;
inline constexpr int32_t kErrOverflow = -75;
inline constexpr int32_t kErrConnReset = -104;
inline constexpr int32_t kErrTimedOut = -110;

struct InterfaceSummary {
    uint8_t interfaceClass{0};
    uint8_t interfaceSubClass{0};
    uint8_t interfaceProtocol{0};
};

struct ExportedDevice {
    std::string busId;
    uint32_t busNum{1};
    uint32_t devNum{1};
    uint32_t speed{kSpeedFull};
    std::vector<uint8_t> deviceDescriptor; // 18 bytes
    std::vector<uint8_t> configDescriptor; // full configuration hierarchy
};

struct CommandHeader {
    uint32_t command{0};
    uint32_t seqnum{0};
    uint32_t devid{0};
    uint32_t direction{0};
    uint32_t endpoint{0};

    // Valid for CMD_SUBMIT
    uint32_t transferFlags{0};
    uint32_t transferBufferLength{0};
    uint32_t startFrame{0};
    uint32_t numberOfPackets{kNoIsoPackets};
    uint32_t interval{0};
    std::array<uint8_t, 8> setup{};

    // Valid for CMD_UNLINK
    uint32_t unlinkSeqnum{0};

    bool IsIsochronous() const noexcept
    {
        return numberOfPackets != 0 && numberOfPackets != kNoIsoPackets;
    }

    uint8_t PipeAddress() const noexcept
    {
        return static_cast<uint8_t>((endpoint & 0x0Fu) | (direction == kDirIn ? 0x80u : 0x00u));
    }
};

struct IsoPacketResult {
    uint32_t offset{0};
    uint32_t actualLength{0};
    int32_t status{0};
};

struct IsoTransferResult {
    std::vector<uint8_t> data;
    std::vector<IsoPacketResult> packets;
};

struct BluetoothHciPipes {
    uint8_t interfaceNumber{0};
    uint8_t eventPipe{0};  // Interrupt IN
    uint8_t aclInPipe{0};  // Bulk IN
    uint8_t aclOutPipe{0}; // Bulk OUT

    bool Valid() const noexcept
    {
        return eventPipe != 0 && aclInPipe != 0 && aclOutPipe != 0;
    }
};

// Big-endian helpers.
uint16_t ReadBe16(std::span<const uint8_t> data, size_t offset);
uint32_t ReadBe32(std::span<const uint8_t> data, size_t offset);
void WriteBe16(std::span<uint8_t> data, size_t offset, uint16_t value);
void WriteBe32(std::span<uint8_t> data, size_t offset, uint32_t value);

// Translates Win32 / WinUSB error codes into USB/IP Linux errno values.
int32_t Win32ErrorToErrno(uint32_t win32Error) noexcept;

// USB descriptor helpers.
std::vector<uint8_t> EnglishLanguageDescriptor();
std::vector<uint8_t> BuildStringDescriptor(std::string_view asciiOrUtf8);
std::vector<InterfaceSummary> ExtractDefaultInterfaces(std::span<const uint8_t> configDescriptor);
BluetoothHciPipes FindBluetoothHciPipes(std::span<const uint8_t> configDescriptor);

// Handshake replies (OP_REP_DEVLIST / OP_REP_IMPORT).
std::vector<uint8_t> EncodeDeviceRecord(const ExportedDevice &dev, bool includeInterfaces);
std::vector<uint8_t> EncodeOpRepDevList(std::span<const ExportedDevice> devices);
std::vector<uint8_t> EncodeOpRepImport(const ExportedDevice *device, uint32_t status);

// URB command header decoding & reply encoding.
std::optional<CommandHeader> DecodeCommandHeader(std::span<const uint8_t> header48);
std::vector<uint8_t> EncodeRetSubmit(
    uint32_t seqnum, int32_t status, uint32_t actualLength, std::span<const uint8_t> data);
std::vector<uint8_t> EncodeRetUnlink(uint32_t seqnum, int32_t status);

// Encodes an isochronous RET_SUBMIT with usbip-win2 front-packed IN payload compaction so
// packet offsets never duplicate SCO audio frames in the host driver.
std::vector<uint8_t> EncodeRetIso(
    uint32_t seqnum, int32_t status, uint32_t startFrame,
    std::span<const uint8_t> cmdIsoDescriptors, uint32_t packetCount, bool isIn,
    const IsoTransferResult *result);

} // namespace Service::Protocol::UsbIp
