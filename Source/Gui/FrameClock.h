// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

#include <QObject>
#include <QString>

#include "Source/Core/DisplayTiming.h"

namespace Gui {

//
// The frame clock: one tick per frame of the display being animated on.
//
// Qt advances its animations from a plain timer at a fixed 60 Hz, whatever the display
// does, so on a 144 Hz display a glide showed 14 frames where the display could have
// drawn 35, and on a 60 Hz one the ticks drifted against the blanks. This installs a
// `QAnimationDriver` in its place: every `QVariantAnimation` and `QPropertyAnimation` in
// the application -- the scroll glide, the switch knob, the popup sliding in -- now
// advances once per vertical blank of the display, at that display's rate, and the
// device loop follows the same clock through `Frame()`.
//
// A worker thread waits on the display (`Core::DisplayTiming::FrameWaiter`) and posts
// each frame to the GUI thread; a frame that arrives while the last is still being drawn
// is merged into it, never queued. The thread sleeps on a condition variable while nothing
// is animating, so an idle application pays nothing for this. Which display is followed
// is decided when a run of animation starts: the active window's, else the one under the
// pointer, else the primary.
//
// One per application, made before any window; `Instance()` is that one, or null in a
// tool that made none, in which case the device loop shows its still.
//
class FrameClock : public QObject
{
    Q_OBJECT

public:
    explicit FrameClock(QObject *parent = nullptr);
    ~FrameClock() override;

    static FrameClock *Instance();

    // A holder keeps the clock ticking; it stops with the last one. GUI thread only.
    void Retain();
    void Release();

Q_SIGNALS:
    // Once per frame of the display, on the GUI thread, while anyone holds the clock. Qt's
    // animations have been advanced by the time this is emitted.
    void Frame();

private:
    class Driver;

    struct Target {
        QString displayName;
        qreal refreshRate{60.0};
    };

    // Shared with the worker thread so even if the destructor has to detach after the
    // exit grace period (e.g. a stalled display), the worker never touches a destroyed
    // `FrameClock` instance.
    struct SharedState {
        std::atomic<int> holders{0};
        std::atomic<bool> pending{false};
        std::atomic<bool> quit{false};
        std::atomic<bool> exited{false};
        std::mutex mutex;
        std::condition_variable wake;
        Target target; // under `mutex`
        FrameClock *owner{nullptr};
    };

    std::unique_ptr<Driver> _driver;
    std::shared_ptr<SharedState> _state{std::make_shared<SharedState>()};
    std::thread _thread;

    static void Run(std::shared_ptr<SharedState> state);
    void OnFrame();
    static Target PickTarget();
};

} // namespace Gui
