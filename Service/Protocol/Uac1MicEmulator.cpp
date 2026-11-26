// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Protocol/Uac1MicEmulator.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Service::Protocol {

namespace {

// Standard USB requests.
constexpr uint8_t kReqGetStatus = 0x00;
constexpr uint8_t kReqClearFeature = 0x01;
constexpr uint8_t kReqGetDescriptor = 0x06;
constexpr uint8_t kReqGetConfiguration = 0x08;
constexpr uint8_t kReqSetConfiguration = 0x09;
constexpr uint8_t kReqGetInterface = 0x0A;
constexpr uint8_t kReqSetInterface = 0x0B;

// UAC1 class-specific requests.
constexpr uint8_t kUacSetCur = 0x01;
constexpr uint8_t kUacGetCur = 0x81;
constexpr uint8_t kUacGetMin = 0x82;
constexpr uint8_t kUacGetMax = 0x83;
constexpr uint8_t kUacGetRes = 0x84;

constexpr uint8_t kFuMuteControl = 0x01;
constexpr uint8_t kFuVolumeControl = 0x02;

float Db256ToLinear(int16_t db256)
{
    if (db256 <= Uac1MicEmulator::kMinVolumeDb256) {
        return 0.0f;
    }
    const float db = static_cast<float>(db256) / 256.0f;
    return std::pow(10.0f, db / 20.0f);
}

} // namespace

Uac1MicEmulator::Uac1MicEmulator(uint64_t bluetoothAddress, std::string deviceName, bool hdVoice)
    : _address{bluetoothAddress}, _deviceName{std::move(deviceName)}, _hdVoice{hdVoice},
      _serial{FormatSerial(bluetoothAddress)}
{
    const std::string truncated = _deviceName.size() > 96 ? _deviceName.substr(0, 96) : _deviceName;
    _product = (_hdVoice ? "HD, " : "A2DP, ") + (truncated.empty() ? "AirPods" : truncated);
    BuildDescriptors();
}

std::string Uac1MicEmulator::FormatSerial(uint64_t bluetoothAddress)
{
    char buf[24]{};
    std::snprintf(
        buf, sizeof(buf), "APBMIC%012llX",
        static_cast<unsigned long long>(bluetoothAddress & 0x0000FFFFFFFFFFFFull));
    return std::string{buf};
}

void Uac1MicEmulator::BuildDescriptors()
{
    // USB 1.1 Device Descriptor (18 bytes).
    _deviceDesc = {
        18,   // bLength
        0x01, // bDescriptorType (DEVICE)
        0x10,
        0x01, // bcdUSB 1.10
        0x00,
        0x00,
        0x00, // Class/SubClass/Protocol per interface
        64,   // bMaxPacketSize0
        static_cast<uint8_t>(kVendorId & 0xFFu),
        static_cast<uint8_t>((kVendorId >> 8) & 0xFFu),
        static_cast<uint8_t>(kProductId & 0xFFu),
        static_cast<uint8_t>((kProductId >> 8) & 0xFFu),
        0x00,
        0x01, // bcdDevice 1.00
        1,    // iManufacturer
        2,    // iProduct
        3,    // iSerialNumber
        1,    // bNumConfigurations
    };

    _stringDescs[0] = UsbIp::EnglishLanguageDescriptor();
    _stringDescs[1] = UsbIp::BuildStringDescriptor("AirpodsBridge");
    _stringDescs[2] = UsbIp::BuildStringDescriptor(_product);
    _stringDescs[3] = UsbIp::BuildStringDescriptor(_serial);
    _stringDescs[4] = UsbIp::BuildStringDescriptor("Headset");

    // Full UAC1 Configuration Descriptor hierarchy:
    //   Interface 0: Audio Control (AC Header + Input Terminal 1 + Feature Unit 2 + Output Term 3)
    //   Interface 1 Alt 0: Audio Streaming (zero-bandwidth idle)
    //   Interface 1 Alt 1: Audio Streaming (48 kHz mono 16-bit PCM, Iso IN EP 0x81, 96 B/ms)
    std::vector<uint8_t> c;
    const auto append = [&](std::initializer_list<uint8_t> bytes) {
        c.insert(c.end(), bytes.begin(), bytes.end());
    };

    // Configuration descriptor (wTotalLength patched below).
    append({9, 0x02, 0, 0, 2, 1, 0, 0x80, 50});
    // Interface 0: Audio Control.
    append({9, 0x04, 0, 0, 0, 0x01, 0x01, 0x00, 0});
    // CS AC Interface Header: wTotalLength = 9 + 12 + 9 + 9 = 39, bInCollection = 1, iface 1.
    append({9, 0x24, 0x01, 0x00, 0x01, 39, 0, 1, 1});
    // Input Terminal 1: Microphone (0x0201), 1 channel mono, iTerminal = 4 ("Headset").
    append({12, 0x24, 0x02, 1, 0x01, 0x02, 0, 1, 0x00, 0x00, 0, 4});
    // Output Terminal 3: USB Streaming (0x0101), bSourceID = 2 (Feature Unit).
    append({9, 0x24, 0x03, 3, 0x01, 0x01, 0, 2, 0});
    // Feature Unit 2: bSourceID = 1 (Input Terminal), bControlSize = 1, Mute + Volume (0x03).
    append({9, 0x24, 0x06, 2, 1, 1, 0x03, 0x00, 0});
    // Interface 1 Alt 0: Audio Streaming, 0 endpoints (idle).
    append({9, 0x04, 1, 0, 0, 0x01, 0x02, 0x00, 0});
    // Interface 1 Alt 1: Audio Streaming, 1 endpoint (active capture).
    append({9, 0x04, 1, 1, 1, 0x01, 0x02, 0x00, 0});
    // CS AS General: bTerminalLink = 3, bDelay = 1, wFormatTag = 0x0001 (PCM).
    append({7, 0x24, 0x01, 3, 1, 0x01, 0x00});
    // CS AS Format Type I: mono, 2 bytes/sample, 16-bit, 1 freq = 48000 Hz (0x00BB80).
    append({11, 0x24, 0x02, 1, 1, 2, 16, 1, 0x80, 0xBB, 0x00});
    // Standard Isochronous IN Endpoint 0x81: synchronous (0x0D), 96 bytes/ms, bInterval = 1 ms.
    append({9, 0x05, 0x81, 0x0D, 96, 0, 1, 0, 0});
    // CS Isochronous Audio Data Endpoint: sampling frequency control.
    append({7, 0x25, 0x01, 0x01, 0, 0, 0});

    const uint16_t totalLen = static_cast<uint16_t>(c.size());
    c[2] = static_cast<uint8_t>(totalLen & 0xFFu);
    c[3] = static_cast<uint8_t>((totalLen >> 8) & 0xFFu);
    _configDesc = std::move(c);
}

UsbIp::ExportedDevice Uac1MicEmulator::MakeExportedDevice(std::string busId, uint32_t devNum) const
{
    return UsbIp::ExportedDevice{
        .busId = std::move(busId),
        .busNum = 1,
        .devNum = devNum,
        .speed = UsbIp::kSpeedFull,
        .deviceDescriptor = _deviceDesc,
        .configDescriptor = _configDesc,
    };
}

void Uac1MicEmulator::SetOnCaptureChanged(std::function<void(bool)> callback)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _onCaptureChanged = std::move(callback);
}

bool Uac1MicEmulator::IsCapturing() const noexcept
{
    return _capturing.load(std::memory_order_relaxed);
}

Uac1MicEmulator::ControlReply Uac1MicEmulator::HandleControlTransfer(
    const std::array<uint8_t, 8> &setup, std::span<const uint8_t> outData)
{
    const uint8_t bmRequestType = setup[0];
    const uint8_t bRequest = setup[1];
    const uint16_t wValue = static_cast<uint16_t>(setup[2]) |
                            static_cast<uint16_t>(static_cast<uint16_t>(setup[3]) << 8);
    const uint16_t wIndex = static_cast<uint16_t>(setup[4]) |
                            static_cast<uint16_t>(static_cast<uint16_t>(setup[5]) << 8);
    const uint16_t wLength = static_cast<uint16_t>(setup[6]) |
                             static_cast<uint16_t>(static_cast<uint16_t>(setup[7]) << 8);

    const uint8_t reqType = static_cast<uint8_t>((bmRequestType >> 5) & 0x03u);

    auto sliceToLen = [wLength](const std::vector<uint8_t> &src) -> ControlReply {
        const size_t n = std::min<size_t>(src.size(), wLength);
        return ControlReply{
            .status = UsbIp::kErrOk,
            .data = std::vector<uint8_t>{src.begin(), src.begin() + static_cast<std::ptrdiff_t>(n)},
        };
    };

    if (reqType == 0) { // Standard USB requests
        switch (bRequest) {
        case kReqGetDescriptor: {
            const uint8_t descType = static_cast<uint8_t>((wValue >> 8) & 0xFFu);
            const uint8_t descIndex = static_cast<uint8_t>(wValue & 0xFFu);
            if (descType == 0x01) { // Device
                return sliceToLen(_deviceDesc);
            }
            if (descType == 0x02) { // Configuration
                return sliceToLen(_configDesc);
            }
            if (descType == 0x03 && descIndex < _stringDescs.size()) { // String
                return sliceToLen(_stringDescs[descIndex]);
            }
            return ControlReply{.status = UsbIp::kErrPipe};
        }

        case kReqGetStatus:
            return ControlReply{.status = UsbIp::kErrOk, .data = {0x00, 0x00}};

        case kReqClearFeature:
            return ControlReply{.status = UsbIp::kErrOk};

        case kReqGetConfiguration: {
            std::lock_guard<std::mutex> lock{_mutex};
            return ControlReply{.status = UsbIp::kErrOk, .data = {_configuration}};
        }

        case kReqSetConfiguration: {
            std::lock_guard<std::mutex> lock{_mutex};
            _configuration = static_cast<uint8_t>(wValue & 0xFFu);
            return ControlReply{.status = UsbIp::kErrOk};
        }

        case kReqGetInterface: {
            std::lock_guard<std::mutex> lock{_mutex};
            const uint8_t alt = (wIndex == 1) ? _altSetting : 0u;
            return ControlReply{.status = UsbIp::kErrOk, .data = {alt}};
        }

        case kReqSetInterface: {
            std::function<void(bool)> cb;
            bool notify = false;
            bool nowCapturing = false;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                if (wIndex == 1) {
                    const uint8_t newAlt = static_cast<uint8_t>(wValue & 0xFFu);
                    if (newAlt > 1) {
                        return ControlReply{.status = UsbIp::kErrPipe};
                    }
                    _altSetting = newAlt;
                    nowCapturing = (_altSetting == 1);
                    const bool prev = _capturing.exchange(nowCapturing);
                    if (prev != nowCapturing) {
                        _writeIndex = 0;
                        _readPos = 0.0;
                        _primed = false;
                        _lastOutSample = 0.0f;
                        _hpPrevIn = 0.0f;
                        _hpPrevOut = 0.0f;
                        notify = true;
                        cb = _onCaptureChanged;
                    }
                }
            }
            if (notify && cb) {
                cb(nowCapturing);
            }
            return ControlReply{.status = UsbIp::kErrOk};
        }

        default:
            return ControlReply{.status = UsbIp::kErrPipe};
        }
    }

    if (reqType == 1) { // Class-specific UAC1 requests
        const uint8_t recipient = static_cast<uint8_t>(bmRequestType & 0x1Fu);
        const uint8_t controlSelector = static_cast<uint8_t>((wValue >> 8) & 0xFFu);
        const uint8_t unitId = static_cast<uint8_t>((wIndex >> 8) & 0xFFu);

        // Endpoint sampling frequency control (48000 Hz = 0x00BB80).
        if (recipient == 0x02) {
            if (bRequest == kUacSetCur) {
                return ControlReply{.status = UsbIp::kErrOk};
            }
            if (bRequest == kUacGetCur) {
                return ControlReply{.status = UsbIp::kErrOk, .data = {0x80, 0xBB, 0x00}};
            }
            return ControlReply{.status = UsbIp::kErrPipe};
        }

        // Feature Unit 2 (Mute & Volume).
        if (unitId == 2) {
            std::lock_guard<std::mutex> lock{_mutex};
            if (controlSelector == kFuMuteControl) {
                if (bRequest == kUacSetCur && !outData.empty()) {
                    _muted = outData[0] != 0;
                    return ControlReply{.status = UsbIp::kErrOk};
                }
                if (bRequest == kUacGetCur) {
                    return ControlReply{
                        .status = UsbIp::kErrOk, .data = {static_cast<uint8_t>(_muted ? 1 : 0)}};
                }
            }
            else if (controlSelector == kFuVolumeControl) {
                auto packI16 = [](int16_t v) -> ControlReply {
                    return ControlReply{
                        .status = UsbIp::kErrOk,
                        .data =
                            {
                                static_cast<uint8_t>(v & 0xFF),
                                static_cast<uint8_t>((v >> 8) & 0xFF),
                            },
                    };
                };
                if (bRequest == kUacSetCur && outData.size() >= 2) {
                    const int16_t vol = static_cast<int16_t>(
                        static_cast<uint16_t>(outData[0]) |
                        (static_cast<uint16_t>(outData[1]) << 8));
                    _volumeDb256 = std::clamp(vol, kMinVolumeDb256, kMaxVolumeDb256);
                    _linearGain = Db256ToLinear(_volumeDb256);
                    return ControlReply{.status = UsbIp::kErrOk};
                }
                if (bRequest == kUacGetCur) {
                    return packI16(_volumeDb256);
                }
                if (bRequest == kUacGetMin) {
                    return packI16(kMinVolumeDb256);
                }
                if (bRequest == kUacGetMax) {
                    return packI16(kMaxVolumeDb256);
                }
                if (bRequest == kUacGetRes) {
                    return packI16(kResVolumeDb256);
                }
            }
        }
    }

    return ControlReply{.status = UsbIp::kErrPipe};
}

void Uac1MicEmulator::PushPcm(int64_t /*samplePosition*/, std::span<const int16_t> samples)
{
    if (!_capturing.load(std::memory_order_relaxed) || samples.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock{_mutex};
    constexpr float kScale = 1.0f / 32768.0f;

    for (int16_t s : samples) {
        const double buffered = static_cast<double>(_writeIndex) - _readPos;
        if (buffered >= static_cast<double>(kRingCapacity - 4)) {
            // Drop oldest sample to prevent wrap overwrite.
            _readPos += 1.0;
            ++_overrunDropped;
        }
        _ring[_writeIndex & kRingMask] = static_cast<float>(s) * kScale;
        ++_writeIndex;
        ++_samplesPushed;
    }

    if (!_primed && (static_cast<double>(_writeIndex) - _readPos) >= kTargetFillSamples) {
        _primed = true;
    }
}

float Uac1MicEmulator::PopFilteredSampleLocked()
{
    const double available = static_cast<double>(_writeIndex) - _readPos;
    float raw = 0.0f;

    if (!_primed || available < 2.0) {
        // Exponential decay Packet Loss Concealment (PLC) avoids harsh clicks on underrun.
        _lastOutSample *= 0.96f;
        if (std::abs(_lastOutSample) < 1e-5f) {
            _lastOutSample = 0.0f;
        }
        raw = _lastOutSample;
        if (_primed) {
            ++_underrunSamples;
        }
    }
    else {
        const uint64_t idx0 = static_cast<uint64_t>(_readPos);
        const float frac = static_cast<float>(_readPos - static_cast<double>(idx0));
        const float s0 = _ring[idx0 & kRingMask];
        const float s1 = _ring[(idx0 + 1) & kRingMask];
        raw = s0 + (s1 - s0) * frac;
        _lastOutSample = raw;

        // Gentle proportional clock-drift servo (+/- 0.25%) keeps buffer near 20 ms target
        // without perceptible pitch shift.
        const double err = available - kTargetFillSamples;
        const double rateAdjust = std::clamp(err * 1.5e-6, -0.0025, 0.0025);
        _readPos += 1.0 + rateAdjust;
        if (_readPos > static_cast<double>(_writeIndex)) {
            _readPos = static_cast<double>(_writeIndex);
        }
    }

    // 1st-order ~60 Hz DC-blocking high-pass filter (alpha = 0.992 at 48 kHz).
    const float hp = 0.992f * (_hpPrevOut + raw - _hpPrevIn);
    _hpPrevIn = raw;
    _hpPrevOut = hp;

    const float gained = _muted ? 0.0f : (hp * _linearGain);
    // Soft tanh limiter protects against digital clipping above +/- 0.95 FS.
    const float limited = std::abs(gained) > 0.95f ? std::tanh(gained) : gained;
    ++_samplesPlayed;
    return limited;
}

UsbIp::IsoTransferResult Uac1MicEmulator::FillIsochronousIn(uint32_t packetCount)
{
    UsbIp::IsoTransferResult res{};
    res.data.resize(static_cast<size_t>(packetCount) * kBytesPerMs);
    res.packets.resize(packetCount);

    std::lock_guard<std::mutex> lock{_mutex};
    for (uint32_t p = 0; p < packetCount; ++p) {
        const uint32_t byteOff = p * kBytesPerMs;
        res.packets[p] = UsbIp::IsoPacketResult{
            .offset = byteOff,
            .actualLength = kBytesPerMs,
            .status = UsbIp::kErrOk,
        };

        for (uint32_t s = 0; s < kSamplesPerMs; ++s) {
            const float f = PopFilteredSampleLocked();
            const int32_t q =
                std::clamp(static_cast<int32_t>(std::lround(f * 32767.0f)), -32768, 32767);
            const int16_t pcm = static_cast<int16_t>(q);
            const size_t pos = static_cast<size_t>(byteOff) + static_cast<size_t>(s) * 2;
            res.data[pos] = static_cast<uint8_t>(pcm & 0xFF);
            res.data[pos + 1] = static_cast<uint8_t>((pcm >> 8) & 0xFF);
        }
    }
    return res;
}

Uac1MicEmulator::AudioStats Uac1MicEmulator::GetStats() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    const double avail = std::max(0.0, static_cast<double>(_writeIndex) - _readPos);
    return AudioStats{
        .samplesPushed = _samplesPushed,
        .samplesPlayed = _samplesPlayed,
        .underrunSamples = _underrunSamples,
        .overrunDropped = _overrunDropped,
        .bufferedSamples = static_cast<uint32_t>(avail),
        .capturing = _capturing.load(std::memory_order_relaxed),
        .muted = _muted,
        .volumeDb256 = _volumeDb256,
    };
}

} // namespace Service::Protocol
