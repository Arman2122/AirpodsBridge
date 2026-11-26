// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <functional>
#include <memory>

namespace Core::SystemTheme {

//
// Whether Windows is set to dark mode for applications (`AppsUseLightTheme`).
//
// Reads the registry each time; it is a handful of microseconds, and never called from a
// hot path.
//
bool IsDark();

//
// Whether Windows' shell surfaces -- the taskbar and notification area -- are dark
// (`SystemUsesLightTheme`). Users on Windows 10/11 can pair a dark taskbar with light apps
// or vice versa, so taskbar-hosted widgets follow this rather than `IsDark()`.
//
bool IsTaskbarDark();

//
// Reports when Windows' app or taskbar mode changes, on the GUI thread.
//
// Windows announces the change by broadcasting a message to every top-level window rather
// than through any callback, so the watcher keeps a hidden window of its own to receive it
// on. It reports only real changes: the broadcast arrives more than once per switch, and
// for other personalisation changes too.
//
class Watcher
{
public:
    using Callback = std::function<void(bool dark)>;

    explicit Watcher(Callback onAppsChanged, Callback onTaskbarChanged = {});
    ~Watcher();

    Watcher(const Watcher &) = delete;
    Watcher &operator=(const Watcher &) = delete;

    // What was last read: the state at construction, or the last change reported.
    bool IsDark() const;
    bool IsTaskbarDark() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Core::SystemTheme
