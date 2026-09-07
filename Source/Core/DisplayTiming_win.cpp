// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/DisplayTiming.h"

#include <chrono>
#include <thread>

#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"

// Both must follow Windows.h, which Source/Support/OS/Windows.h brings in.
#include <d3dkmthk.h>
#include <dwmapi.h>

namespace Core::DisplayTiming {

namespace {

using Clock = std::chrono::steady_clock;

// A blank that takes this long is not coming: the display is asleep, or the session is
// remote and never had one. The wait is written off and the timer takes over.
constexpr auto kStallLimit = std::chrono::milliseconds{250};

// A wait that returns sooner than this did not wait for a frame: no display refreshes
// that fast. This many in a row and the source is spinning, not waiting.
constexpr auto kHastyLimit = std::chrono::milliseconds{1};
constexpr int kHastyRun = 3;

// Behind by more than this and the timer resyncs to now rather than firing a burst of
// catch-up frames.
constexpr int kMaxCatchUpFrames = 2;

// Arrived with the Windows 10 1803 SDK; older headers lack it.
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
    #define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

bool Succeeded(NTSTATUS status)
{
    return status >= 0;
}

} // namespace

const char *ToString(Source source)
{
    switch (source) {
    case Source::VerticalBlank:
        return "vertical blank";
    case Source::Compositor:
        return "compositor";
    case Source::Timer:
    default:
        return "timer";
    }
}

class FrameWaiter::Impl
{
public:
    Impl(const QString &displayName, qreal refreshRate)
        : _period{std::chrono::duration_cast<Clock::duration>(
              std::chrono::duration<double>{1.0 / (refreshRate > 1.0 ? refreshRate : 60.0)})}
    {
        // The display's adapter and its video present source: what the kernel's graphics
        // scheduler counts blanks on.
        const HDC dc = CreateDCW(nullptr, displayName.toStdWString().c_str(), nullptr, nullptr);
        if (dc != nullptr) {
            D3DKMT_OPENADAPTERFROMHDC open{};
            open.hDc = dc;
            if (Succeeded(D3DKMTOpenAdapterFromHdc(&open))) {
                _adapter = open.hAdapter;
                _presentSource = open.VidPnSourceId;
                _source = Source::VerticalBlank;
            }
            DeleteDC(dc);
        }

        if (_source != Source::VerticalBlank) {
            BOOL composed = FALSE;
            if (SUCCEEDED(DwmIsCompositionEnabled(&composed)) && composed != FALSE) {
                _source = Source::Compositor;
            }
        }

        // The timer, whichever source is in use: it is what a stalled source falls to.
        // The high-resolution kind is sub-millisecond; the plain kind is what the machine
        // has otherwise.
        _timer = CreateWaitableTimerExW(
            nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (_timer == nullptr) {
            _timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        _next = Clock::now() + _period;
    }

    ~Impl()
    {
        if (_adapter != 0) {
            D3DKMT_CLOSEADAPTER close{};
            close.hAdapter = _adapter;
            D3DKMTCloseAdapter(&close);
        }
        if (_timer != nullptr) {
            CloseHandle(_timer);
        }
    }

    Source GetSource() const
    {
        return _source;
    }

    void WaitForNextFrame()
    {
        if (_source != Source::Timer) {
            const auto before = Clock::now();
            const bool delivered =
                _source == Source::VerticalBlank ? WaitForVerticalBlank() : SUCCEEDED(DwmFlush());
            const auto now = Clock::now();
            const auto took = now - before;
            if (delivered && took < kStallLimit) {
                // A source that keeps answering at once is not waiting on anything -- a
                // compositor with no frames to flush, in a remote session -- and would
                // spin the thread. A few in a row and it is written off like a stall.
                _hasty = took < kHastyLimit ? _hasty + 1 : 0;
                if (_hasty < kHastyRun) {
                    _next = now + _period;
                    return;
                }
                LOG(Warn, "DisplayTiming: the {} returns without waiting; timing frames instead.",
                    ToString(_source));
            }
            else {
                LOG(Warn, "DisplayTiming: the {} stopped delivering frames; timing them instead.",
                    ToString(_source));
            }
            _source = Source::Timer;
            _next = now + _period;
        }

        // Paced from the previous boundary rather than from now, so the fraction of a
        // millisecond in the period accumulates instead of being lost every frame.
        const auto now = Clock::now();
        if (_next + _period * kMaxCatchUpFrames < now) {
            _next = now;
        }
        if (_next > now) {
            LARGE_INTEGER due;
            due.QuadPart =
                -std::chrono::duration_cast<std::chrono::nanoseconds>(_next - now).count() / 100;
            if (_timer != nullptr &&
                SetWaitableTimer(_timer, &due, 0, nullptr, nullptr, FALSE) != FALSE)
            {
                WaitForSingleObject(_timer, INFINITE);
            }
            else {
                std::this_thread::sleep_until(_next);
            }
        }
        _next += _period;
    }

private:
    Source _source{Source::Timer};
    D3DKMT_HANDLE _adapter{0};
    D3DDDI_VIDEO_PRESENT_SOURCE_ID _presentSource{0};
    HANDLE _timer{nullptr};
    int _hasty{0}; // consecutive waits that returned at once
    Clock::duration _period;
    Clock::time_point _next;

    bool WaitForVerticalBlank()
    {
        D3DKMT_WAITFORVERTICALBLANKEVENT wait{};
        wait.hAdapter = _adapter;
        wait.VidPnSourceId = _presentSource;
        return Succeeded(D3DKMTWaitForVerticalBlankEvent(&wait));
    }
};

FrameWaiter::FrameWaiter(const QString &displayName, qreal refreshRate)
    : _impl{std::make_unique<Impl>(displayName, refreshRate)}
{
}

FrameWaiter::~FrameWaiter() = default;

Source FrameWaiter::GetSource() const
{
    return _impl->GetSource();
}

void FrameWaiter::WaitForNextFrame()
{
    _impl->WaitForNextFrame();
}

} // namespace Core::DisplayTiming
