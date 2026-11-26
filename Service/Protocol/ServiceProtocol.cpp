// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Protocol/ServiceProtocol.h"

#include <algorithm>
#include <cstring>

namespace Service::Protocol {

namespace {

class Writer
{
public:
    explicit Writer(size_t reserveBytes = 64)
    {
        _buf.reserve(reserveBytes);
    }

    void U8(uint8_t v)
    {
        _buf.push_back(v);
    }

    void Bool(bool v)
    {
        U8(v ? 1u : 0u);
    }

    void U16(uint16_t v)
    {
        _buf.push_back(static_cast<uint8_t>(v & 0xFFu));
        _buf.push_back(static_cast<uint8_t>((v >> 8) & 0xFFu));
    }

    void U32(uint32_t v)
    {
        for (int i = 0; i < 4; ++i) {
            _buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
        }
    }

    void U64(uint64_t v)
    {
        for (int i = 0; i < 8; ++i) {
            _buf.push_back(static_cast<uint8_t>((v >> (i * 8)) & 0xFFu));
        }
    }

    void I64(int64_t v)
    {
        U64(static_cast<uint64_t>(v));
    }

    void String(const std::string &s)
    {
        const uint32_t len = static_cast<uint32_t>(std::min<size_t>(s.size(), 4096));
        U32(len);
        _buf.insert(
            _buf.end(), reinterpret_cast<const uint8_t *>(s.data()),
            reinterpret_cast<const uint8_t *>(s.data()) + len);
    }

    void Bytes(std::span<const uint8_t> data)
    {
        const uint32_t len = static_cast<uint32_t>(data.size());
        U32(len);
        _buf.insert(_buf.end(), data.begin(), data.end());
    }

    std::vector<uint8_t> Take()
    {
        return std::move(_buf);
    }

private:
    std::vector<uint8_t> _buf;
};

class Reader
{
public:
    explicit Reader(std::span<const uint8_t> data) : _data{data} {}

    bool U8(uint8_t &out)
    {
        if (_pos + 1 > _data.size()) {
            return false;
        }
        out = _data[_pos++];
        return true;
    }

    bool Bool(bool &out)
    {
        uint8_t v = 0;
        if (!U8(v)) {
            return false;
        }
        out = v != 0;
        return true;
    }

    bool U16(uint16_t &out)
    {
        if (_pos + 2 > _data.size()) {
            return false;
        }
        out = static_cast<uint16_t>(_data[_pos]) |
              static_cast<uint16_t>(static_cast<uint16_t>(_data[_pos + 1]) << 8);
        _pos += 2;
        return true;
    }

    bool U32(uint32_t &out)
    {
        if (_pos + 4 > _data.size()) {
            return false;
        }
        out = static_cast<uint32_t>(_data[_pos]) | (static_cast<uint32_t>(_data[_pos + 1]) << 8) |
              (static_cast<uint32_t>(_data[_pos + 2]) << 16) |
              (static_cast<uint32_t>(_data[_pos + 3]) << 24);
        _pos += 4;
        return true;
    }

    bool U64(uint64_t &out)
    {
        if (_pos + 8 > _data.size()) {
            return false;
        }
        uint64_t v = 0;
        for (int i = 0; i < 8; ++i) {
            v |= static_cast<uint64_t>(_data[_pos + i]) << (i * 8);
        }
        _pos += 8;
        out = v;
        return true;
    }

    bool I64(int64_t &out)
    {
        uint64_t v = 0;
        if (!U64(v)) {
            return false;
        }
        out = static_cast<int64_t>(v);
        return true;
    }

    bool String(std::string &out)
    {
        uint32_t len = 0;
        if (!U32(len) || len > 4096 || _pos + len > _data.size()) {
            return false;
        }
        out.assign(reinterpret_cast<const char *>(_data.data() + _pos), len);
        _pos += len;
        return true;
    }

    bool Bytes(std::vector<uint8_t> &out, uint32_t maxLen = kMaxFramePayloadBytes)
    {
        uint32_t len = 0;
        if (!U32(len) || len > maxLen || _pos + len > _data.size()) {
            return false;
        }
        out.assign(
            _data.begin() + static_cast<std::ptrdiff_t>(_pos),
            _data.begin() + static_cast<std::ptrdiff_t>(_pos + len));
        _pos += len;
        return true;
    }

private:
    std::span<const uint8_t> _data;
    size_t _pos{0};
};

} // namespace

const char *ToString(RadioServiceState state) noexcept
{
    switch (state) {
    case RadioServiceState::Stopped:
        return "stopped";
    case RadioServiceState::Starting:
        return "starting";
    case RadioServiceState::PreflightMissingUsbip:
        return "missing_usbip_driver";
    case RadioServiceState::NoUsbAdapter:
        return "no_usb_adapter";
    case RadioServiceState::NonUsbAdapterOnly:
        return "non_usb_adapter_only";
    case RadioServiceState::GuardTripped:
        return "guard_tripped";
    case RadioServiceState::BindingWinUsb:
        return "binding_winusb";
    case RadioServiceState::Running:
        return "running";
    case RadioServiceState::RestoringNative:
        return "restoring_native";
    case RadioServiceState::Faulted:
        return "faulted";
    default:
        return "unknown";
    }
}

std::vector<uint8_t> EncodeFrame(MessageType type, std::span<const uint8_t> payload)
{
    const uint32_t len =
        static_cast<uint32_t>(std::min<size_t>(payload.size(), kMaxFramePayloadBytes));
    std::vector<uint8_t> out;
    out.reserve(kFrameHeaderBytes + len);
    out.insert(out.end(), kFrameMagic.begin(), kFrameMagic.end());
    out.push_back(static_cast<uint8_t>(kProtocolVersion & 0xFFu));
    out.push_back(static_cast<uint8_t>((kProtocolVersion >> 8) & 0xFFu));
    const uint16_t rawType = static_cast<uint16_t>(type);
    out.push_back(static_cast<uint8_t>(rawType & 0xFFu));
    out.push_back(static_cast<uint8_t>((rawType >> 8) & 0xFFu));
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>((len >> (i * 8)) & 0xFFu));
    }
    out.insert(out.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(len));
    return out;
}

DecodeStatus
TryDecodeFrame(std::span<const uint8_t> streamBytes, Frame &outFrame, size_t &outConsumedBytes)
{
    outConsumedBytes = 0;
    if (streamBytes.size() < kFrameHeaderBytes) {
        return DecodeStatus::NeedMoreData;
    }

    if (!std::equal(kFrameMagic.begin(), kFrameMagic.end(), streamBytes.begin())) {
        return DecodeStatus::InvalidFrame;
    }

    const uint16_t version = static_cast<uint16_t>(streamBytes[4]) |
                             static_cast<uint16_t>(static_cast<uint16_t>(streamBytes[5]) << 8);
    if (version != kProtocolVersion) {
        return DecodeStatus::InvalidFrame;
    }

    const uint16_t rawType = static_cast<uint16_t>(streamBytes[6]) |
                             static_cast<uint16_t>(static_cast<uint16_t>(streamBytes[7]) << 8);
    const uint32_t payloadSize = static_cast<uint32_t>(streamBytes[8]) |
                                 (static_cast<uint32_t>(streamBytes[9]) << 8) |
                                 (static_cast<uint32_t>(streamBytes[10]) << 16) |
                                 (static_cast<uint32_t>(streamBytes[11]) << 24);

    if (payloadSize > kMaxFramePayloadBytes) {
        return DecodeStatus::InvalidFrame;
    }

    const size_t totalBytes = kFrameHeaderBytes + static_cast<size_t>(payloadSize);
    if (streamBytes.size() < totalBytes) {
        return DecodeStatus::NeedMoreData;
    }

    outFrame.type = static_cast<MessageType>(rawType);
    outFrame.payload.assign(
        streamBytes.begin() + static_cast<std::ptrdiff_t>(kFrameHeaderBytes),
        streamBytes.begin() + static_cast<std::ptrdiff_t>(totalBytes));
    outConsumedBytes = totalBytes;
    return DecodeStatus::Ok;
}

std::vector<uint8_t> EncodeAddressPayload(uint64_t bluetoothAddress)
{
    Writer w{8};
    w.U64(bluetoothAddress);
    return w.Take();
}

std::optional<uint64_t> DecodeAddressPayload(std::span<const uint8_t> payload)
{
    Reader r{payload};
    uint64_t addr = 0;
    if (!r.U64(addr)) {
        return std::nullopt;
    }
    return addr;
}

std::vector<uint8_t> EncodeProxyConfig(const ProxyConfig &cfg)
{
    Writer w{16};
    w.Bool(cfg.lowLatencyQos);
    w.U32(cfg.pollLatencyUs);
    w.Bool(cfg.declineHfp);
    w.Bool(cfg.virtualMicEnabled);
    return w.Take();
}

std::optional<ProxyConfig> DecodeProxyConfig(std::span<const uint8_t> payload)
{
    Reader r{payload};
    ProxyConfig cfg{};
    if (!r.Bool(cfg.lowLatencyQos) || !r.U32(cfg.pollLatencyUs) || !r.Bool(cfg.declineHfp) ||
        !r.Bool(cfg.virtualMicEnabled))
    {
        return std::nullopt;
    }
    return cfg;
}

std::vector<uint8_t> EncodeStatusSnapshot(const StatusSnapshot &snap)
{
    Writer w{256};
    w.U8(static_cast<uint8_t>(snap.state));
    w.Bool(snap.usbipDriverReady);
    w.Bool(snap.winUsbBound);
    w.Bool(snap.virtualAdapterAttached);
    w.Bool(snap.guardTripped);
    w.Bool(snap.callAudioActive);
    w.Bool(snap.virtualMicAttached);
    w.Bool(snap.virtualMicCapturing);
    w.Bool(snap.lowLatencyQosEnabled);
    w.Bool(snap.declineHfpEnabled);
    w.U16(snap.adapterVid);
    w.U16(snap.adapterPid);
    w.U16(snap.hciManufacturer);
    w.U32(snap.activeAapChannels);
    w.U32(snap.activePollLatencyUs);
    w.U32(snap.recentFailures);
    w.U64(snap.injectedPackets);
    w.U64(snap.interceptedPackets);
    w.String(snap.adapterInstanceId);
    w.String(snap.adapterDisplayName);
    w.String(snap.detail);
    return w.Take();
}

std::optional<StatusSnapshot> DecodeStatusSnapshot(std::span<const uint8_t> payload)
{
    Reader r{payload};
    StatusSnapshot snap{};
    uint8_t rawState = 0;
    if (!r.U8(rawState) || !r.Bool(snap.usbipDriverReady) || !r.Bool(snap.winUsbBound) ||
        !r.Bool(snap.virtualAdapterAttached) || !r.Bool(snap.guardTripped) ||
        !r.Bool(snap.callAudioActive) || !r.Bool(snap.virtualMicAttached) ||
        !r.Bool(snap.virtualMicCapturing) || !r.Bool(snap.lowLatencyQosEnabled) ||
        !r.Bool(snap.declineHfpEnabled) || !r.U16(snap.adapterVid) || !r.U16(snap.adapterPid) ||
        !r.U16(snap.hciManufacturer) || !r.U32(snap.activeAapChannels) ||
        !r.U32(snap.activePollLatencyUs) || !r.U32(snap.recentFailures) ||
        !r.U64(snap.injectedPackets) || !r.U64(snap.interceptedPackets) ||
        !r.String(snap.adapterInstanceId) || !r.String(snap.adapterDisplayName) ||
        !r.String(snap.detail))
    {
        return std::nullopt;
    }
    snap.state = static_cast<RadioServiceState>(rawState);
    return snap;
}

std::vector<uint8_t> EncodeAapChannelState(const AapChannelStateMsg &msg)
{
    Writer w{16};
    w.U64(msg.bluetoothAddress);
    w.Bool(msg.connected);
    w.U32(msg.errorCode);
    return w.Take();
}

std::optional<AapChannelStateMsg> DecodeAapChannelState(std::span<const uint8_t> payload)
{
    Reader r{payload};
    AapChannelStateMsg msg{};
    if (!r.U64(msg.bluetoothAddress) || !r.Bool(msg.connected) || !r.U32(msg.errorCode)) {
        return std::nullopt;
    }
    return msg;
}

std::vector<uint8_t> EncodeAapPacket(const AapPacketMsg &msg)
{
    Writer w{12 + msg.data.size()};
    w.U64(msg.bluetoothAddress);
    w.Bytes(msg.data);
    return w.Take();
}

std::optional<AapPacketMsg> DecodeAapPacket(std::span<const uint8_t> payload)
{
    Reader r{payload};
    AapPacketMsg msg{};
    if (!r.U64(msg.bluetoothAddress) || !r.Bytes(msg.data, kMaxAapPacketBytes)) {
        return std::nullopt;
    }
    return msg;
}

std::vector<uint8_t> EncodeConfigureVirtualMic(const ConfigureVirtualMicMsg &msg)
{
    Writer w{32 + msg.deviceName.size()};
    w.U64(msg.bluetoothAddress);
    w.Bool(msg.enable);
    w.Bool(msg.hdVoice);
    w.String(msg.deviceName);
    return w.Take();
}

std::optional<ConfigureVirtualMicMsg> DecodeConfigureVirtualMic(std::span<const uint8_t> payload)
{
    Reader r{payload};
    ConfigureVirtualMicMsg msg{};
    if (!r.U64(msg.bluetoothAddress) || !r.Bool(msg.enable) || !r.Bool(msg.hdVoice) ||
        !r.String(msg.deviceName))
    {
        return std::nullopt;
    }
    return msg;
}

std::vector<uint8_t> EncodeMicPcm(const MicPcmMsg &msg)
{
    Writer w{12 + msg.pcmBytes.size()};
    w.I64(msg.samplePosition);
    w.Bytes(msg.pcmBytes);
    return w.Take();
}

std::optional<MicPcmMsg> DecodeMicPcm(std::span<const uint8_t> payload)
{
    Reader r{payload};
    MicPcmMsg msg{};
    if (!r.I64(msg.samplePosition) || !r.Bytes(msg.pcmBytes)) {
        return std::nullopt;
    }
    return msg;
}

std::vector<uint8_t> EncodeMicCaptureState(const MicCaptureStateMsg &msg)
{
    Writer w{12};
    w.U64(msg.bluetoothAddress);
    w.Bool(msg.capturing);
    return w.Take();
}

std::optional<MicCaptureStateMsg> DecodeMicCaptureState(std::span<const uint8_t> payload)
{
    Reader r{payload};
    MicCaptureStateMsg msg{};
    if (!r.U64(msg.bluetoothAddress) || !r.Bool(msg.capturing)) {
        return std::nullopt;
    }
    return msg;
}

std::vector<uint8_t> EncodeCallAudioState(const CallAudioStateMsg &msg)
{
    Writer w{12};
    w.U64(msg.bluetoothAddress);
    w.Bool(msg.active);
    return w.Take();
}

std::optional<CallAudioStateMsg> DecodeCallAudioState(std::span<const uint8_t> payload)
{
    Reader r{payload};
    CallAudioStateMsg msg{};
    if (!r.U64(msg.bluetoothAddress) || !r.Bool(msg.active)) {
        return std::nullopt;
    }
    return msg;
}

std::vector<uint8_t> EncodeCommandAck(const CommandAckMsg &msg)
{
    Writer w{16 + msg.message.size()};
    w.U16(static_cast<uint16_t>(msg.requestType));
    w.Bool(msg.ok);
    w.String(msg.message);
    return w.Take();
}

std::optional<CommandAckMsg> DecodeCommandAck(std::span<const uint8_t> payload)
{
    Reader r{payload};
    CommandAckMsg msg{};
    uint16_t rawReq = 0;
    if (!r.U16(rawReq) || !r.Bool(msg.ok) || !r.String(msg.message)) {
        return std::nullopt;
    }
    msg.requestType = static_cast<MessageType>(rawReq);
    return msg;
}

} // namespace Service::Protocol
