// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <functional>
#include <memory>

namespace Core::SystemEvents {

//
// Zero-polling listener for Windows session lifecycle broadcasts on the GUI thread:
//   - `WM_POWERBROADCAST` (`PBT_APMSUSPEND` / `PBT_APMRESUMEAUTOMATIC`) so BLE scanning
//     pauses cleanly across sleep/Modern Standby and Bluetooth state reconciles on wake.
//   - `TaskbarCreated` so taskbar-hosted widgets re-attach cleanly if `explorer.exe`
//     restarts.
//
class Watcher
{
public:
    struct Callbacks {
        std::function<void()> onSuspend;
        std::function<void()> onResume;
        std::function<void()> onTaskbarCreated;
    };

    explicit Watcher(Callbacks callbacks);
    ~Watcher();

    Watcher(const Watcher &) = delete;
    Watcher &operator=(const Watcher &) = delete;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Core::SystemEvents
