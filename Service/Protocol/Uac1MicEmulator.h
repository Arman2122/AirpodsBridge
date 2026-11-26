// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "Service/Protocol/UsbIpProtocol.h"

namespace Service::Protocol {

//
// Virtual USB Audio Class 1.0 (UAC1) Microphone Device Emulator.
//
// Presented to Windows via the Microsoft-signed `usbip-win2` virtual USB host controller so
// Windows automatically loads its inbox `usbaudio.sys` driver -- creating a native stereo-
// safe recording endpoint ("Headset (A2DP, <AirPods Name>)") with zero custom audio drivers.
//
// Features an adaptive clock-drift jitter buffer (smooth micro-resampling between the AirPods
// Bluetooth clock and Windows' 1 ms USB SOF clock), fade-out Packet Loss Concealment (PLC) on
// underruns, a 60 Hz DC-blocking high-pass filter, soft tanh limiting, and full UAC1 Feature
// Unit mute/volume support.
//
class Uac1MicEmulator
{
public:
    static constexpr uint16_t kVendorId = 0x1209;  // pid.codes open-source VID
    static constexpr uint16_t kProductId = 0x0001; // shared test PID (disambiguated by serial)
    static constexpr uint32_t kSampleRate = 48000;
    static constexpr uint32_t kSamplesPerMs = 48;
    static constexpr uint32_t kBytesPerMs = kSamplesPerMs * sizeof(int16_t); // 96 bytes/ms
    static constexpr int16_t kMinVolumeDb256 = -60 * 256;
    static constexpr int16_t kMaxVolumeDb256 = 0;
    static constexpr int16_t kResVolumeDb256 = 256; // 1 dB steps

    struct ControlReply {
        int32_t status{UsbIp::kErrOk};
        std::vector<uint8_t> data;
    };

    struct AudioStats {
        uint64_t samplesPushed{0};
        uint64_t samplesPlayed{0};
        uint64_t underrunSamples{0};
        uint64_t overrunDropped{0};
        uint32_t bufferedSamples{0};
        bool capturing{false};
        bool muted{false};
        int16_t volumeDb256{0};
    };

    Uac1MicEmulator(uint64_t bluetoothAddress, std::string deviceName, bool hdVoice = false);

    uint64_t BluetoothAddress() const noexcept
    {
        return _address;
    }
    const std::string &Serial() const noexcept
    {
        return _serial;
    }
    const std::string &ProductString() const noexcept
    {
        return _product;
    }
    const std::vector<uint8_t> &DeviceDescriptor() const noexcept
    {
        return _deviceDesc;
    }
    const std::vector<uint8_t> &ConfigDescriptor() const noexcept
    {
        return _configDesc;
    }

    UsbIp::ExportedDevice MakeExportedDevice(std::string busId, uint32_t devNum = 2) const;

    void SetOnCaptureChanged(std::function<void(bool capturing)> callback);
    bool IsCapturing() const noexcept;

    // Handles USB EP0 standard & UAC1 class-specific requests.
    ControlReply
    HandleControlTransfer(const std::array<uint8_t, 8> &setup, std::span<const uint8_t> outData);

    // Feeds mono 48 kHz s16le PCM decoded from the AirPods sideband/voice stream.
    void PushPcm(int64_t samplePosition, std::span<const int16_t> samples);

    // Produces `packetCount` 1 ms USB isochronous IN frames (96 bytes each).
    UsbIp::IsoTransferResult FillIsochronousIn(uint32_t packetCount);

    AudioStats GetStats() const;

    static std::string FormatSerial(uint64_t bluetoothAddress);

private:
    static constexpr size_t kRingCapacity = 16384; // ~341 ms power-of-two capacity
    static constexpr size_t kRingMask = kRingCapacity - 1;
    static constexpr double kTargetFillSamples = 48.0 * 20.0; // 20 ms target jitter cushion

    void BuildDescriptors();
    float PopFilteredSampleLocked();

    uint64_t _address{0};
    std::string _deviceName;
    bool _hdVoice{false};
    std::string _serial;
    std::string _product;

    std::vector<uint8_t> _deviceDesc;
    std::vector<uint8_t> _configDesc;
    std::array<std::vector<uint8_t>, 5> _stringDescs;

    mutable std::mutex _mutex;
    std::function<void(bool)> _onCaptureChanged;
    uint8_t _configuration{0};
    uint8_t _altSetting{0};
    std::atomic<bool> _capturing{false};
    bool _muted{false};
    int16_t _volumeDb256{0};
    float _linearGain{1.0f};

    // Ring buffer + fractional micro-resampler + PLC + DC-block high-pass state.
    std::array<float, kRingCapacity> _ring{};
    uint64_t _writeIndex{0};
    double _readPos{0.0};
    float _lastOutSample{0.0f};
    float _hpPrevIn{0.0f};
    float _hpPrevOut{0.0f};
    bool _primed{false};

    uint64_t _samplesPushed{0};
    uint64_t _samplesPlayed{0};
    uint64_t _underrunSamples{0};
    uint64_t _overrunDropped{0};
};

} // namespace Service::Protocol
