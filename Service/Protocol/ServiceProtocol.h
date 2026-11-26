// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Service::Protocol {

//
// Versioned binary wire protocol carried over the local Named Pipe between the native x64
// `AirpodsBridgeService` and the Win32 `AirpodsBridge` UI application.
//
// Frame layout (all fields little-endian):
//   [0..3]  Magic       : 'A', 'P', 'B', 'R'
//   [4..5]  Version     : kProtocolVersion (1)
//   [6..7]  MessageType : uint16_t
//   [8..11] PayloadSize : uint32_t (<= kMaxFramePayloadBytes)
//   [12..]  Payload     : PayloadSize bytes
//

inline constexpr std::array<uint8_t, 4> kFrameMagic{'A', 'P', 'B', 'R'};
inline constexpr uint16_t kProtocolVersion = 1;
inline constexpr size_t kFrameHeaderBytes = 12;
inline constexpr uint32_t kMaxFramePayloadBytes = 64u * 1024u;
inline constexpr size_t kMaxAapPacketBytes = 4096;
inline constexpr wchar_t kPipeNameW[] = L"\\\\.\\pipe\\AirpodsBridge.RadioService.v1";

enum class RadioServiceState : uint8_t {
    Stopped = 0,
    Starting = 1,
    PreflightMissingUsbip = 2,
    NoUsbAdapter = 3,
    NonUsbAdapterOnly = 4,
    GuardTripped = 5,
    BindingWinUsb = 6,
    Running = 7,
    RestoringNative = 8,
    Faulted = 9,
};

enum class MessageType : uint16_t {
    // Client -> Service
    Hello = 0x0001,
    QueryStatus = 0x0002,
    OpenAapChannel = 0x0010,
    CloseAapChannel = 0x0011,
    SendAapPacket = 0x0012,
    SetProxyConfig = 0x0020,
    ConfigureVirtualMic = 0x0021,
    PushMicPcm = 0x0022,
    RetryAfterGuard = 0x0030,
    RestoreNativeDriver = 0x0031,

    // Service -> Client
    StatusSnapshot = 0x8001,
    AapChannelState = 0x8010,
    AapPacketReceived = 0x8011,
    MicCaptureState = 0x8020,
    CallAudioState = 0x8021,
    CommandAck = 0x8030,
};

struct Frame {
    MessageType type{MessageType::Hello};
    std::vector<uint8_t> payload;
};

struct ProxyConfig {
    bool lowLatencyQos{true};
    uint32_t pollLatencyUs{11250};
    bool declineHfp{false};
    bool virtualMicEnabled{false};

    bool operator==(const ProxyConfig &) const = default;
};

struct StatusSnapshot {
    RadioServiceState state{RadioServiceState::Stopped};
    bool usbipDriverReady{false};
    bool winUsbBound{false};
    bool virtualAdapterAttached{false};
    bool guardTripped{false};
    bool callAudioActive{false};
    bool virtualMicAttached{false};
    bool virtualMicCapturing{false};
    bool lowLatencyQosEnabled{true};
    bool declineHfpEnabled{false};
    uint16_t adapterVid{0};
    uint16_t adapterPid{0};
    uint16_t hciManufacturer{0};
    uint32_t activeAapChannels{0};
    uint32_t activePollLatencyUs{0};
    uint32_t recentFailures{0};
    uint64_t injectedPackets{0};
    uint64_t interceptedPackets{0};
    std::string adapterInstanceId;
    std::string adapterDisplayName;
    std::string detail;

    bool operator==(const StatusSnapshot &) const = default;
};

struct AapChannelStateMsg {
    uint64_t bluetoothAddress{0};
    bool connected{false};
    uint32_t errorCode{0};

    bool operator==(const AapChannelStateMsg &) const = default;
};

struct AapPacketMsg {
    uint64_t bluetoothAddress{0};
    std::vector<uint8_t> data;

    bool operator==(const AapPacketMsg &) const = default;
};

struct ConfigureVirtualMicMsg {
    uint64_t bluetoothAddress{0};
    bool enable{false};
    bool hdVoice{false};
    std::string deviceName;

    bool operator==(const ConfigureVirtualMicMsg &) const = default;
};

struct MicPcmMsg {
    int64_t samplePosition{0};
    std::vector<uint8_t> pcmBytes;

    bool operator==(const MicPcmMsg &) const = default;
};

struct MicCaptureStateMsg {
    uint64_t bluetoothAddress{0};
    bool capturing{false};

    bool operator==(const MicCaptureStateMsg &) const = default;
};

struct CallAudioStateMsg {
    uint64_t bluetoothAddress{0};
    bool active{false};

    bool operator==(const CallAudioStateMsg &) const = default;
};

struct CommandAckMsg {
    MessageType requestType{MessageType::Hello};
    bool ok{false};
    std::string message;

    bool operator==(const CommandAckMsg &) const = default;
};

const char *ToString(RadioServiceState state) noexcept;

// Frame encoding & incremental stream decoding.
std::vector<uint8_t> EncodeFrame(MessageType type, std::span<const uint8_t> payload);

enum class DecodeStatus : uint8_t {
    NeedMoreData = 0,
    Ok = 1,
    InvalidFrame = 2,
};

DecodeStatus
TryDecodeFrame(std::span<const uint8_t> streamBytes, Frame &outFrame, size_t &outConsumedBytes);

// Typed payload serializers / deserializers.
std::vector<uint8_t> EncodeAddressPayload(uint64_t bluetoothAddress);
std::optional<uint64_t> DecodeAddressPayload(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeProxyConfig(const ProxyConfig &cfg);
std::optional<ProxyConfig> DecodeProxyConfig(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeStatusSnapshot(const StatusSnapshot &snap);
std::optional<StatusSnapshot> DecodeStatusSnapshot(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeAapChannelState(const AapChannelStateMsg &msg);
std::optional<AapChannelStateMsg> DecodeAapChannelState(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeAapPacket(const AapPacketMsg &msg);
std::optional<AapPacketMsg> DecodeAapPacket(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeConfigureVirtualMic(const ConfigureVirtualMicMsg &msg);
std::optional<ConfigureVirtualMicMsg> DecodeConfigureVirtualMic(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeMicPcm(const MicPcmMsg &msg);
std::optional<MicPcmMsg> DecodeMicPcm(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeMicCaptureState(const MicCaptureStateMsg &msg);
std::optional<MicCaptureStateMsg> DecodeMicCaptureState(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeCallAudioState(const CallAudioStateMsg &msg);
std::optional<CallAudioStateMsg> DecodeCallAudioState(std::span<const uint8_t> payload);

std::vector<uint8_t> EncodeCommandAck(const CommandAckMsg &msg);
std::optional<CommandAckMsg> DecodeCommandAck(std::span<const uint8_t> payload);

} // namespace Service::Protocol
