// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <memory>

#include <QString>

namespace Core::DisplayTiming {

//
// How a frame is waited for. `VerticalBlank` is the display's own signal: the moment the
// scan-out of one frame ends and the compositor takes the next, per display, at that
// display's rate. `Compositor` is the desktop compositor's flush, which follows the
// primary display. `Timer` is a clock paced at the display's nominal rate, with no
// relation to its phase: correct in frequency, not in alignment.
//
enum class Source { VerticalBlank, Compositor, Timer };

const char *ToString(Source source);

//
// Waits for the frames of one display.
//
// Built once per run of animation for the display being animated on. It takes the best
// source the machine offers and steps down when that source fails or stalls -- a display
// that has gone to sleep stops delivering blanks -- so a wait never hangs the caller for
// more than a frame or so. Blocking; meant for a thread of its own.
//
class FrameWaiter
{
public:
    // `displayName` is what `QScreen::name()` gives ("\\.\DISPLAY1"); `refreshRate` in Hz
    // is what the timer paces at when the display itself cannot be waited on.
    FrameWaiter(const QString &displayName, qreal refreshRate);
    ~FrameWaiter();

    FrameWaiter(const FrameWaiter &) = delete;
    FrameWaiter &operator=(const FrameWaiter &) = delete;

    Source GetSource() const;

    // Blocks until the display's next frame.
    void WaitForNextFrame();

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Core::DisplayTiming
