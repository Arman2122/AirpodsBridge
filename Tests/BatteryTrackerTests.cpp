// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <chrono>

#include <QtTest>

#include "Source/Domain/BatteryTracker.h"

using namespace Core::AirPods;
using namespace std::chrono_literals;

class BatteryTrackerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void EstimatesDischargingTimeRemaining();
    void EstimatesChargingTimeToFull();
    void IgnoresNoiseAndResetsOnModeSwitch();
    void DetectsAsymmetricDrainAfterStreak();
    void TracksOnePieceHeadsets();
    void FormatsDurationsCleanly();
};

void BatteryTrackerTests::EstimatesDischargingTimeRemaining()
{
    BatteryTracker tracker;
    const auto t0 = BatteryTracker::Clock::now();

    State st;
    st.model = Model::AirPods_Pro_3;
    st.pods.left.battery = 80;
    st.pods.right.battery = 90;

    auto first = tracker.Observe(st, t0);
    QVERIFY(!first.minutesRemaining.has_value());

    // Drop 10% in 30 minutes on left (210 min remaining for 70%), 10% on right (240 min for 80%).
    st.pods.left.battery = 70;
    st.pods.right.battery = 80;
    auto second = tracker.Observe(st, t0 + 30min);
    QVERIFY(second.minutesRemaining.has_value());
    // Limiting active bud is left (~210 minutes).
    QCOMPARE(second.minutesRemaining.value(), 210);
}

void BatteryTrackerTests::EstimatesChargingTimeToFull()
{
    BatteryTracker tracker;
    const auto t0 = BatteryTracker::Clock::now();

    State st;
    st.model = Model::AirPods_Pro_3;
    st.pods.left.battery = 40;
    st.pods.left.isCharging = true;
    st.pods.right.battery = 60;
    st.pods.right.isCharging = true;

    tracker.Observe(st, t0);

    // Both gain 20% in 10 minutes -> left needs 40% more (20m), right needs 20% more (10m).
    st.pods.left.battery = 60;
    st.pods.right.battery = 80;
    const auto insights = tracker.Observe(st, t0 + 10min);
    QVERIFY(insights.minutesToFull.has_value());
    QCOMPARE(insights.minutesToFull.value(), 20);
}

void BatteryTrackerTests::IgnoresNoiseAndResetsOnModeSwitch()
{
    BatteryTracker tracker;
    const auto t0 = BatteryTracker::Clock::now();

    State st;
    st.model = Model::AirPods_Pro_3;
    st.pods.left.battery = 70;
    tracker.Observe(st, t0);

    // Upward glitch while discharging should reset the anchor rather than produce junk.
    st.pods.left.battery = 80;
    auto glitch = tracker.Observe(st, t0 + 10min);
    QVERIFY(!glitch.minutesRemaining.has_value());

    // Switching to charging also resets estimate.
    st.pods.left.isCharging = true;
    auto switched = tracker.Observe(st, t0 + 20min);
    QVERIFY(!switched.minutesRemaining.has_value());
    QVERIFY(!switched.minutesToFull.has_value());
}

void BatteryTrackerTests::DetectsAsymmetricDrainAfterStreak()
{
    BatteryTracker tracker;
    const auto t0 = BatteryTracker::Clock::now();

    State st;
    st.model = Model::AirPods_Pro_3;
    st.pods.left.battery = 90;
    st.pods.right.battery = 50; // 40% gap >= 30%

    // Single sample is not enough to trigger (protects against transient stale side).
    QVERIFY(!tracker.Observe(st, t0).asymmetricWarning);
    // Second consecutive sample confirms sustained asymmetry.
    QVERIFY(tracker.Observe(st, t0 + 1min).asymmetricWarning);

    // Once balanced, warning clears immediately.
    st.pods.right.battery = 80;
    QVERIFY(!tracker.Observe(st, t0 + 2min).asymmetricWarning);
}

void BatteryTrackerTests::TracksOnePieceHeadsets()
{
    BatteryTracker tracker;
    const auto t0 = BatteryTracker::Clock::now();

    State st;
    st.model = Model::AirPods_Max_USB_C;
    st.pods.left.battery = 80;
    tracker.Observe(st, t0);

    st.pods.left.battery = 70;
    const auto insights = tracker.Observe(st, t0 + 60min);
    QVERIFY(insights.minutesRemaining.has_value());
    QCOMPARE(insights.minutesRemaining.value(), 420);
    QVERIFY(!insights.asymmetricWarning);
}

void BatteryTrackerTests::FormatsDurationsCleanly()
{
    QCOMPARE(BatteryTracker::FormatDuration(0), QString{});
    QCOMPARE(BatteryTracker::FormatDuration(25), QStringLiteral("~25m"));
    QCOMPARE(BatteryTracker::FormatDuration(60), QStringLiteral("~1h"));
    QCOMPARE(BatteryTracker::FormatDuration(155), QStringLiteral("~2h 35m"));
}

QTEST_MAIN(BatteryTrackerTests)
#include "BatteryTrackerTests.moc"
