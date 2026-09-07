// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

//
// Checks the frame clock that drives every animation: that Qt's animations run and
// finish under it, that it ticks while held at something like a display's rate and not
// at all when let go, and that it is the one instance the application can find.
//
// Whether the ticks land on the display's vertical blanks cannot be asserted here -- a
// test runner may have no display worth the name -- so the rate bounds are loose, and
// the log line the clock writes on a real machine is the evidence for that.
//

#include <QtTest>

#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QVariantAnimation>

#include "Source/Gui/FrameClock.h"

namespace {

void Run(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

class FrameClockTests : public QObject
{
    Q_OBJECT

private slots:
    void drivesQtAnimationsToCompletion();
    void ticksWhileHeldAndRestsWhenReleased();
    void instanceIsTheOneMade();
};

void FrameClockTests::drivesQtAnimationsToCompletion()
{
    Gui::FrameClock clock;

    QVariantAnimation animation;
    animation.setStartValue(0.0);
    animation.setEndValue(1.0);
    animation.setDuration(200);

    int changes = 0;
    connect(&animation, &QVariantAnimation::valueChanged, this, [&] { ++changes; });

    QEventLoop loop;
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    connect(&animation, &QVariantAnimation::finished, &loop, &QEventLoop::quit);
    animation.start();
    loop.exec();

    QCOMPARE(animation.state(), QAbstractAnimation::Stopped);
    QCOMPARE(animation.currentValue().toDouble(), 1.0);
    // A 200 ms animation on any display is many frames; a clock that advanced it once,
    // or only at its end, would be broken.
    QVERIFY2(changes >= 4, qPrintable(QString{"only %1 value changes"}.arg(changes)));
}

void FrameClockTests::ticksWhileHeldAndRestsWhenReleased()
{
    Gui::FrameClock clock;

    int frames = 0;
    connect(&clock, &Gui::FrameClock::Frame, this, [&] { ++frames; });

    constexpr int kHoldMs = 400;
    clock.Retain();
    Run(kHoldMs);
    clock.Release();
    const int held = frames;

    const auto *screen = QGuiApplication::primaryScreen();
    const qreal rate =
        screen != nullptr && screen->refreshRate() > 1.0 ? screen->refreshRate() : 60.0;
    const qreal expected = rate * kHoldMs / 1000.0;
    // Loose on purpose: a test runner may be slow, headless, or timing the frames itself.
    QVERIFY2(
        held >= expected / 4,
        qPrintable(QString{"%1 frames in %2 ms at %3 Hz"}.arg(held).arg(kHoldMs).arg(rate)));
    QVERIFY2(
        held <= expected * 2 + 2,
        qPrintable(QString{"%1 frames in %2 ms at %3 Hz"}.arg(held).arg(kHoldMs).arg(rate)));

    // Let go: a frame already posted but not yet dispatched may land, and so may the one
    // the worker posts as its last wait returns; nothing after those.
    Run(200);
    QVERIFY2(frames - held <= 2, qPrintable(QString{"%1 frames after release"}.arg(frames - held)));
}

void FrameClockTests::instanceIsTheOneMade()
{
    QVERIFY(Gui::FrameClock::Instance() == nullptr);
    {
        Gui::FrameClock clock;
        QCOMPARE(Gui::FrameClock::Instance(), &clock);
    }
    QVERIFY(Gui::FrameClock::Instance() == nullptr);
}

QTEST_MAIN(FrameClockTests)
#include "FrameClockTests.moc"
