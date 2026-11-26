// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Service/Protocol/UsbIpProtocol.h"
#include "Service/Win/WinHandles.h"

namespace Service::Win {

//
// Native WinUSB transport wrapping the physical USB Bluetooth adapter.
//
// Manages the primary WinUSB interface plus every associated USB interface (e.g. Interface 1
// for SCO/eSCO isochronous voice audio), executes overlapped control/bulk/interrupt/isoch
// transfers, and supports clean abort on USB/IP CMD_UNLINK or service shutdown.
//
class WinUsbRadio
{
public:
    WinUsbRadio() = default;
    ~WinUsbRadio();

    WinUsbRadio(const WinUsbRadio &) = delete;
    WinUsbRadio &operator=(const WinUsbRadio &) = delete;

    bool Open(const std::wstring &devicePath);
    void Close();
    bool IsOpen() const noexcept;

    const std::vector<uint8_t> &DeviceDescriptor() const noexcept
    {
        return _deviceDesc;
    }
    const std::vector<uint8_t> &ConfigDescriptor() const noexcept
    {
        return _configDesc;
    }
    uint32_t UsbIpSpeed() const noexcept
    {
        return _usbIpSpeed;
    }
    const Protocol::UsbIp::BluetoothHciPipes &HciPipes() const noexcept
    {
        return _hciPipes;
    }

    Protocol::UsbIp::ExportedDevice MakeExportedDevice(std::string busId) const;

    // Control transfer on EP0 (OUT or IN depending on setup[0] bit 7).
    // Returns Win32 error code (0 = success).
    uint32_t ControlTransfer(
        const std::array<uint8_t, 8> &setup, std::span<const uint8_t> outData,
        std::vector<uint8_t> &inData, uint32_t &transferred, HANDLE cancelEvent = nullptr);

    // Selects alternate setting on a USB interface (e.g. Interface 1 alt 0..6 for SCO).
    uint32_t SetInterfaceAltSetting(uint8_t interfaceNumber, uint8_t altSetting);

    // Bulk / Interrupt pipe transfers (returns Win32 error code, 0 = success).
    uint32_t ReadPipe(
        uint8_t pipeId, uint32_t maxLength, std::vector<uint8_t> &outBuffer,
        HANDLE cancelEvent = nullptr);
    uint32_t WritePipe(
        uint8_t pipeId, std::span<const uint8_t> data, uint32_t &written,
        HANDLE cancelEvent = nullptr);

    // Isochronous pipe transfers (SCO/eSCO call audio).
    uint32_t TransferIsoch(
        uint8_t pipeId, bool isIn, std::span<const uint8_t> outData, uint32_t packetCount,
        uint32_t bytesPerPacket, Protocol::UsbIp::IsoTransferResult &result,
        HANDLE cancelEvent = nullptr);

    uint32_t ResetPipe(uint8_t pipeId);
    void AbortAllPipes();

    static bool IsFatalDeviceLossError(uint32_t win32Error) noexcept;

private:
    WINUSB_INTERFACE_HANDLE InterfaceForPipeLocked(uint8_t pipeId);
    void RefreshPipeMapLocked();
    void ConfigureHciPoliciesLocked();

    mutable std::mutex _ifaceMutex;
    UniqueHandle _file;
    UniqueWinUsb _primary;
    std::unordered_map<uint8_t, UniqueWinUsb> _associated; // ifaceNumber -> handle
    std::unordered_map<uint8_t, WINUSB_INTERFACE_HANDLE> _pipeToInterface;

    std::vector<uint8_t> _deviceDesc;
    std::vector<uint8_t> _configDesc;
    uint32_t _usbIpSpeed{Protocol::UsbIp::kSpeedFull};
    Protocol::UsbIp::BluetoothHciPipes _hciPipes{};
    std::atomic<bool> _open{false};
};

} // namespace Service::Win
