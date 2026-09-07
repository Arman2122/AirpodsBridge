// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/FrameClock.h"

#include <chrono>

#include <QAnimationDriver>
#include <QApplication>
#include <QCursor>
#include <QScreen>
#include <QWidget>

#include "Source/Support/Logger.h"

namespace Gui {

namespace {

FrameClock *g_instance = nullptr;

// How long the destructor waits for the worker before letting it go: a frame or two on
// any display, unless a display has stopped delivering them.
constexpr auto kExitGrace = std::chrono::milliseconds{250};

} // namespace

//
// What Qt's animation machinery talks to. It starts when the first animation does and
// stops with the last; each is one hold on the clock. The base class keeps the time that
// `elapsed()` reports, so it has to see start and stop as well.
//
class FrameClock::Driver final : public QAnimationDriver
{
public:
    explicit Driver(FrameClock &clock) : _clock{clock} {}

protected:
    void start() override
    {
        QAnimationDriver::start();
        _clock.Retain();
    }

    void stop() override
    {
        QAnimationDriver::stop();
        _clock.Release();
    }

private:
    FrameClock &_clock;
};

FrameClock::FrameClock(QObject *parent) : QObject{parent}, _driver{std::make_unique<Driver>(*this)}
{
    _driver->install();
    _thread = std::thread{[this] { Run(); }};
    g_instance = this;
}

FrameClock::~FrameClock()
{
    g_instance = nullptr;
    _driver->uninstall();

    _quit = true;
    _wake.notify_all();
    // Bounded: a worker stuck waiting on a display that will never blank again must not
    // hold the process at exit.
    const auto deadline = std::chrono::steady_clock::now() + kExitGrace;
    while (!_exited && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
    if (_exited) {
        _thread.join();
    }
    else {
        LOG(Warn, "FrameClock: the display never delivered the last frame; leaving the wait.");
        _thread.detach();
    }
}

FrameClock *FrameClock::Instance()
{
    return g_instance;
}

void FrameClock::Retain()
{
    if (_holders.fetch_add(1) != 0) {
        return;
    }
    // The first holder starts a run: decide which display it is on, then wake the worker.
    {
        const std::lock_guard<std::mutex> lock{_mutex};
        _target = PickTarget();
    }
    _wake.notify_one();
}

void FrameClock::Release()
{
    // The worker sees the count reach zero after its next frame and goes back to sleep.
    _holders.fetch_sub(1);
}

FrameClock::Target FrameClock::PickTarget()
{
    QScreen *screen = nullptr;
    if (auto *active = QApplication::activeWindow(); active != nullptr) {
        screen = active->screen();
    }
    if (screen == nullptr) {
        screen = QGuiApplication::screenAt(QCursor::pos());
    }
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }

    Target target;
    if (screen != nullptr) {
        target.displayName = screen->name();
        target.refreshRate = screen->refreshRate();
    }
    return target;
}

void FrameClock::Run()
{
    QString lastDisplay;
    Core::DisplayTiming::Source lastSource = Core::DisplayTiming::Source::Timer;
    bool first = true;

    while (true) {
        Target target;
        {
            std::unique_lock<std::mutex> lock{_mutex};
            _wake.wait(lock, [this] { return _holders.load() > 0 || _quit.load(); });
            if (_quit) {
                break;
            }
            target = _target;
        }

        Core::DisplayTiming::FrameWaiter waiter{target.displayName, target.refreshRate};
        if (first || target.displayName != lastDisplay || waiter.GetSource() != lastSource) {
            LOG(Info, "FrameClock: following {} at {:.1f} Hz through the {}.", target.displayName,
                target.refreshRate, Core::DisplayTiming::ToString(waiter.GetSource()));
            first = false;
            lastDisplay = target.displayName;
            lastSource = waiter.GetSource();
        }

        while (_holders.load() > 0 && !_quit.load()) {
            waiter.WaitForNextFrame();
            // One frame in flight at a time: if the GUI thread has not drawn the last
            // one yet, this one merges into it rather than queueing behind it.
            if (!_pending.exchange(true)) {
                QMetaObject::invokeMethod(this, [this] { OnFrame(); }, Qt::QueuedConnection);
            }
        }
    }

    _exited = true;
}

void FrameClock::OnFrame()
{
    _pending = false;
    if (_driver->isRunning()) {
        _driver->advance();
    }
    Q_EMIT Frame();
}

} // namespace Gui
