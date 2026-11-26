// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <string>

namespace Service::Win {

//
// Native, completely silent (zero PowerShell, zero console window) elevated setup runner
// embedded directly inside `AirpodsBridgeService.exe`.
//
// Writes live JSON progress updates to `C:\ProgramData\AirpodsBridge\SetupProgress.json`
// so the GUI renders a native progress bar and exact status/error feedback without ever
// flashing a terminal window.
//
class NativeSetup
{
public:
    static constexpr wchar_t kProgressFilePath[] =
        L"C:\\ProgramData\\AirpodsBridge\\SetupProgress.json";

    // Installs/enables the No-Test-Mode Control Engine:
    //   1. Checks physical USB Bluetooth adapter compatibility.
    //   2. Verifies Microsoft-signed `usbip-win2` (downloads + SHA-256 verifies + silently
    //      installs only if missing).
    //   3. Copies `AirpodsBridgeService.exe` to Program Files, registers SCM service with
    //      failure recovery, and starts it.
    // Returns 0 on success, 3010 if a reboot is recommended, or 1 on failure.
    static int RunEnable();

    // Stops service, restores native Bluetooth driver on all bound adapters, unregisters SCM
    // service, and removes staged binaries.
    static int RunDisable();

    // Emergency rollback: stops service and restores native vendor Bluetooth driver.
    static int RunRestoreNative();
};

} // namespace Service::Win
