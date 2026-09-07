// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <functional>
#include <memory>

namespace Core::SystemTheme {

//
// Whether Windows is set to dark mode for applications.
//
// That is the "Choose your default app mode" switch under Settings > Personalisation >
// Colours. The taskbar and Start have a switch of their own, which this does not read:
// the application's windows are apps, and follow the app setting the way every other
// application does.
//
// Reads the registry each time; it is a handful of microseconds, and never called from a
// hot path.
//
bool IsDark();

//
// Reports when that setting changes, on the GUI thread.
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

    explicit Watcher(Callback onChanged);
    ~Watcher();

    Watcher(const Watcher &) = delete;
    Watcher &operator=(const Watcher &) = delete;

    // What was last read: the state at construction, or the last change reported.
    bool IsDark() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Core::SystemTheme
