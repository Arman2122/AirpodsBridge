// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Service::Win {

struct AdapterCandidate {
    std::wstring instanceId;
    std::string displayName;
    uint16_t vid{0};
    uint16_t pid{0};
    bool isCompositeFunction{false};
    bool onWinUsb{false};
};

enum class AdapterSearchStatus : uint8_t {
    Found = 0,
    NoAdapter = 1,
    NonUsbOnly = 2,
};

struct AdapterSearchResult {
    AdapterSearchStatus status{AdapterSearchStatus::NoAdapter};
    std::optional<AdapterCandidate> adapter;
};

//
// Native x64 Windows PnP/SetupAPI Bluetooth adapter driver manager.
//
// Switches a physical USB Bluetooth adapter (whole USB device or composite function &MI_nn)
// between Windows' built-in WinUSB driver (`%SystemRoot%\INF\winusb.inf`) and its best
// compatible native vendor driver (`DIF_SELECTBESTCOMPATDRV`), with pre-flight verification
// of the signed `usbip-win2` (`usbip2_ude`) host controller driver.
//
class RadioDriverManager
{
public:
    static bool IsUsbipDriverReady();
    static std::optional<std::wstring> FindUsbipExecutable();

    static AdapterSearchResult FindPhysicalAdapter();
    static bool IsPresent(const std::wstring &instanceId);
    static bool IsOnWinUsb(const std::wstring &instanceId);
    static bool NeedsRestart(const std::wstring &instanceId);
    static bool IsCompositeFunction(const std::wstring &instanceId);
    static std::wstring DevicePathForWinUsb(const std::wstring &instanceId);
    static std::string DescribeDeviceState(const std::wstring &instanceId);

    // Win32 481 (ERROR_PNP_QUERY_REMOVE_DEVICE_TIMEOUT): indicates a driver hung in
    // IRP_MN_QUERY_REMOVE_DEVICE (e.g. BTH\MS_RFCOMM). Callers must NEVER retry DiInstallDevice
    // on top of a wedged devnode or they risk poisoning kernel PnP until shutdown (BSOD 0x9F/0x4).
    static constexpr uint32_t kErrorPnpQueryRemoveTimeout = 481u;

    // Switches the adapter to inbox WinUSB; returns true on success (sets `rebootRequired`).
    static bool BindWinUsb(
        const std::wstring &instanceId, bool &rebootRequired, uint32_t *win32ErrorOut = nullptr);

    // Restores the best-ranked native vendor driver; returns true on success.
    static bool RestoreNative(
        const std::wstring &instanceId, bool &rebootRequired, uint32_t *win32ErrorOut = nullptr);

    // Re-plugs the USB hub port (IOCTL_USB_HUB_CYCLE_PORT) so a newly installed driver starts
    // immediately even if a process held an open handle.
    static bool CycleHubPort(const std::wstring &instanceId);

    // Marks an unplugged device with CONFIGFLAG_REINSTALL so Windows restores its native driver
    // on next plug-in.
    static bool MarkForReinstall(const std::wstring &instanceId);

    // Restores every adapter instance ever bound to WinUSB by this service. Returns true only
    // when every present bound adapter was restored (or already native).
    static bool RestoreAllBoundAdapters();

    // Cleans up phantom Bluetooth audio devnodes on virtual adapter instances so Windows does
    // not number endpoints ("2- AirPods Pro"), while preserving pairings, HID, and LE nodes.
    static uint32_t ForgetHiddenVirtualAudioNodes(const std::wstring &physicalInstanceId);

    // Copies BLE Device IDs (VID/PID/Rev) from BTHLEDevice nodes into BTHPORT\Parameters\Devices
    // so BLE peripherals paired while virtualized retain HID functionality.
    static uint32_t CarryLeDeviceIds();

    static bool ParseVidPid(const std::wstring &instanceId, uint16_t &vidOut, uint16_t &pidOut);
};

} // namespace Service::Win
