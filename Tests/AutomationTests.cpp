// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <chrono>

#include <QtTest>

#include "Source/Core/AutomationEngine.h"
#include "Source/Domain/AutomationPolicies.h"

using namespace Core::Automation;
using Core::AirPods::State;

namespace {

State MakeState(
    bool leftInEar, bool rightInEar, uint32_t leftLevel = 80, uint32_t rightLevel = 80,
    uint32_t caseLevel = 80)
{
    State state;
    state.pods.left.isInEar = leftInEar;
    state.pods.right.isInEar = rightInEar;
    state.pods.left.battery = leftLevel;
    state.pods.right.battery = rightLevel;
    state.caseBox.battery = caseLevel;
    return state;
}

class RecordingEffects final : public Effects
{
public:
    void PlayMedia() override
    {
        actions.append("play");
    }
    void PauseMedia() override
    {
        actions.append("pause");
    }
    void Notify(const QString &title, const QString &text) override
    {
        notifications.append(title + ": " + text);
    }
    void ConnectDevice() override
    {
        ++connectRequests;
    }
    void SwitchAudioOutputToAirPods() override
    {
        routeActions.append("airpods");
    }
    void RestorePreviousAudioOutput() override
    {
        routeActions.append("restore");
    }

    QStringList actions;
    QStringList notifications;
    QStringList routeActions;
    int connectRequests{0};
};

} // namespace

class AutomationTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void EarDetectionPausesWhenABudComesOutAndResumesWhenBothAreIn();
    void EarDetectionCanPauseWithoutResuming();
    void EarDetectionSingleBudFollowsTheLastBud();
    void EarDetectionOnePieceWorksOnEitherSide();
    void EarDetectionNeedsAPreviousState();
    void DisconnectPausesOnlyOnARealDrop();
    void BatteryAlertsOncePerDropAndRearms();
    void BatteryAlertsOnePieceDeviceOnce();
    void BatteryAlertIsSilentWhenOff();
    void AutoConnectRespectsStateAndCooldown();
    void FullChargeAlertFiresOnceAndRearmsBelowThreshold();
    void SmartAudioRoutingSwitchesOnWearAndRestoresOnRemoval();
    void EngineCarriesDecisionsToEffects();
    void BleWearJitterIsDebouncedAndPostDisconnectSuppressesLoop();
};

void AutomationTests::EarDetectionPausesWhenABudComesOutAndResumesWhenBothAreIn()
{
    EarDetectionOptions options;
    const auto worn = MakeState(true, true);
    const auto oneOut = MakeState(true, false);
    const auto bothOut = MakeState(false, false);

    QCOMPARE(EarDetectionPolicy::OnStateChanged(worn, oneOut, options), MediaAction::Pause);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(oneOut, bothOut, options), MediaAction::None);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(bothOut, oneOut, options), MediaAction::None);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(oneOut, worn, options), MediaAction::Play);

    options.enabled = false;
    QCOMPARE(EarDetectionPolicy::OnStateChanged(worn, oneOut, options), MediaAction::None);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(oneOut, worn, options), MediaAction::None);
}

void AutomationTests::EarDetectionCanPauseWithoutResuming()
{
    EarDetectionOptions options;
    options.resumePlayback = false;
    const auto worn = MakeState(true, true);
    const auto oneOut = MakeState(true, false);

    QCOMPARE(EarDetectionPolicy::OnStateChanged(worn, oneOut, options), MediaAction::Pause);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(oneOut, worn, options), MediaAction::None);
}

void AutomationTests::EarDetectionSingleBudFollowsTheLastBud()
{
    EarDetectionOptions options;
    options.singleBud = true;
    const auto worn = MakeState(true, true);
    const auto oneOut = MakeState(true, false);
    const auto bothOut = MakeState(false, false);

    // One bud is still an ear: taking the other out changes nothing.
    QCOMPARE(EarDetectionPolicy::OnStateChanged(worn, oneOut, options), MediaAction::None);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(oneOut, bothOut, options), MediaAction::Pause);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(bothOut, oneOut, options), MediaAction::Play);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(oneOut, worn, options), MediaAction::None);
}

void AutomationTests::EarDetectionOnePieceWorksOnEitherSide()
{
    const EarDetectionOptions options; // singleBud = false (default)
    auto worn = MakeState(true, false);
    worn.model = Core::AirPods::Model::AirPods_Max;
    auto removed = MakeState(false, false);
    removed.model = Core::AirPods::Model::AirPods_Max;

    QVERIFY(EarDetectionPolicy::IsWorn(worn, false));
    QCOMPARE(EarDetectionPolicy::OnStateChanged(worn, removed, options), MediaAction::Pause);
    QCOMPARE(EarDetectionPolicy::OnStateChanged(removed, worn, options), MediaAction::Play);
}

void AutomationTests::EarDetectionNeedsAPreviousState()
{
    const EarDetectionOptions options;
    QCOMPARE(
        EarDetectionPolicy::OnStateChanged(std::nullopt, MakeState(false, false), options),
        MediaAction::None);
}

void AutomationTests::DisconnectPausesOnlyOnARealDrop()
{
    DisconnectPolicy policy;

    // The device reports itself disconnected once at every start.
    QCOMPARE(policy.OnConnectionChanged(false, true), MediaAction::None);
    QCOMPARE(policy.OnConnectionChanged(true, true), MediaAction::None);
    QCOMPARE(policy.OnConnectionChanged(false, true), MediaAction::Pause);
    QCOMPARE(policy.OnConnectionChanged(false, true), MediaAction::None);

    // Tracked while the rule is off, so turning it on does not misread the next report.
    QCOMPARE(policy.OnConnectionChanged(true, false), MediaAction::None);
    QCOMPARE(policy.OnConnectionChanged(false, false), MediaAction::None);
    QCOMPARE(policy.OnConnectionChanged(true, true), MediaAction::None);
    QCOMPARE(policy.OnConnectionChanged(false, true), MediaAction::Pause);
}

void AutomationTests::BatteryAlertsOncePerDropAndRearms()
{
    BatteryAlertPolicy policy;
    const BatteryAlertOptions options; // threshold 20

    QVERIFY(policy.OnStateChanged(MakeState(true, true, 50, 50, 50), options).empty());

    auto alerts = policy.OnStateChanged(MakeState(true, true, 20, 50, 50), options);
    QCOMPARE(alerts.size(), size_t{1});
    QCOMPARE(alerts.front(), (BatteryAlert{BatteryComponent::Left, 20}));

    // Hovering around the line does not alert again.
    QVERIFY(policy.OnStateChanged(MakeState(true, true, 19, 50, 50), options).empty());
    QVERIFY(policy.OnStateChanged(MakeState(true, true, 22, 50, 50), options).empty());
    QVERIFY(policy.OnStateChanged(MakeState(true, true, 18, 50, 50), options).empty());

    // Climbing back above the threshold by the margin re-arms it.
    QVERIFY(policy.OnStateChanged(MakeState(true, true, 26, 50, 50), options).empty());
    alerts = policy.OnStateChanged(MakeState(true, true, 15, 50, 50), options);
    QCOMPARE(alerts.size(), size_t{1});
    QCOMPARE(alerts.front().level, uint32_t{15});

    // Charging re-arms it too.
    auto charging = MakeState(true, true, 10, 50, 50);
    charging.pods.left.isCharging = true;
    QVERIFY(policy.OnStateChanged(charging, options).empty());
    alerts = policy.OnStateChanged(MakeState(true, true, 10, 50, 50), options);
    QCOMPARE(alerts.size(), size_t{1});

    // Several components at once, in a fixed order.
    policy.Reset();
    alerts = policy.OnStateChanged(MakeState(true, true, 5, 6, 7), options);
    QCOMPARE(alerts.size(), size_t{3});
    QCOMPARE(alerts.at(0).component, BatteryComponent::Left);
    QCOMPARE(alerts.at(1).component, BatteryComponent::Right);
    QCOMPARE(alerts.at(2).component, BatteryComponent::Case);

    // An unknown level is left alone.
    policy.Reset();
    auto unknown = MakeState(true, true);
    unknown.pods.left.battery = {};
    QVERIFY(policy.OnStateChanged(unknown, options).empty());
}

void AutomationTests::BatteryAlertsOnePieceDeviceOnce()
{
    BatteryAlertPolicy policy;
    const BatteryAlertOptions options; // threshold 20

    // AirPods Max mirroring 15% on both sides must produce exactly one Device alert.
    auto maxLow = MakeState(true, false, 15, 15, 10);
    maxLow.model = Core::AirPods::Model::AirPods_Max_USB_C;
    maxLow.displayName = "AirPods Max (USB-C)";

    const auto alerts = policy.OnStateChanged(maxLow, options);
    QCOMPARE(alerts.size(), size_t{1});
    QCOMPARE(alerts.front(), (BatteryAlert{BatteryComponent::Device, 15}));

    // And the engine uses the device name rather than "Left AirPod".
    RecordingEffects effects;
    Engine engine{effects};
    engine.SetLowBatteryThreshold(20);
    engine.OnStateUpdated(maxLow);
    QCOMPARE(effects.notifications.size(), 1);
    QVERIFY(effects.notifications.first().contains("AirPods Max (USB-C)"));
    QVERIFY(!effects.notifications.first().contains("Left"));
}

void AutomationTests::BatteryAlertIsSilentWhenOff()
{
    BatteryAlertPolicy policy;
    BatteryAlertOptions options;
    options.enabled = false;

    QVERIFY(policy.OnStateChanged(MakeState(true, true, 10, 50, 50), options).empty());

    // Turning it on with a battery already low alerts once, straight away.
    options.enabled = true;
    QCOMPARE(policy.OnStateChanged(MakeState(true, true, 10, 50, 50), options).size(), size_t{1});
    QVERIFY(policy.OnStateChanged(MakeState(true, true, 10, 50, 50), options).empty());
}

void AutomationTests::AutoConnectRespectsStateAndCooldown()
{
    AutoConnectPolicy policy;
    const auto start = AutoConnectPolicy::Clock::now();

    QVERIFY(!policy.OnLidOpenedNearby(false, false, start)); // rule off
    QVERIFY(!policy.OnLidOpenedNearby(true, true, start));   // already connected
    QVERIFY(policy.OnLidOpenedNearby(false, true, start));
    QVERIFY(!policy.OnLidOpenedNearby(false, true, start + std::chrono::seconds{5}));
    QVERIFY(policy.OnLidOpenedNearby(false, true, start + AutoConnectPolicy::kCooldown));
}

void AutomationTests::FullChargeAlertFiresOnceAndRearmsBelowThreshold()
{
    FullChargeAlertPolicy policy;

    auto charging80 = MakeState(false, false, 80, 80, 50);
    charging80.pods.left.isCharging = true;
    charging80.pods.right.isCharging = true;

    auto charging100 = MakeState(false, false, 100, 100, 50);
    charging100.pods.left.isCharging = true;
    charging100.pods.right.isCharging = true;

    // Starting at 100% without having been observed below full must not fire immediately.
    QVERIFY(!policy.OnStateChanged(charging100, true));

    // Drop to 80% charging arms it; reaching 100% fires exactly once.
    QVERIFY(!policy.OnStateChanged(charging80, true));
    QVERIFY(policy.OnStateChanged(charging100, true));
    QVERIFY(!policy.OnStateChanged(charging100, true));

    // Brief 95% flicker does not re-arm; dropping to 90% does.
    auto charging95 = MakeState(false, false, 95, 100, 50);
    charging95.pods.left.isCharging = true;
    QVERIFY(!policy.OnStateChanged(charging95, true));
    QVERIFY(!policy.OnStateChanged(charging100, true));

    auto charging90 = MakeState(false, false, 90, 100, 50);
    charging90.pods.left.isCharging = true;
    QVERIFY(!policy.OnStateChanged(charging90, true));
    QVERIFY(policy.OnStateChanged(charging100, true));
}

void AutomationTests::SmartAudioRoutingSwitchesOnWearAndRestoresOnRemoval()
{
    SmartAudioRoutePolicy policy;
    const auto unworn = MakeState(false, false);
    const auto worn = MakeState(true, true);

    // Connecting while already worn switches playback to AirPods immediately.
    QCOMPARE(
        policy.OnStateChanged(std::nullopt, worn, true, false), AudioRouteAction::SwitchToAirPods);
    // Stay worn -> no duplicate switch.
    QCOMPARE(policy.OnStateChanged(worn, worn, true, false), AudioRouteAction::None);
    // Removed -> restore previous speakers.
    QCOMPARE(policy.OnStateChanged(worn, unworn, true, false), AudioRouteAction::RestorePrevious);
    // Already restored -> disconnect does nothing.
    QCOMPARE(policy.OnDisconnected(true), AudioRouteAction::None);

    // Worn then sudden Bluetooth disconnect restores speakers too.
    QCOMPARE(policy.OnStateChanged(unworn, worn, true, false), AudioRouteAction::SwitchToAirPods);
    QCOMPARE(policy.OnDisconnected(true), AudioRouteAction::RestorePrevious);
}

void AutomationTests::EngineCarriesDecisionsToEffects()
{
    RecordingEffects effects;
    Engine engine{effects};
    engine.SetLowBatteryThreshold(20);

    // The start-up report is not a drop.
    engine.OnDeviceConnectionChanged(false);
    QVERIFY(effects.actions.isEmpty());

    engine.OnDeviceConnectionChanged(true);
    engine.OnStateUpdated(MakeState(true, true));
    QVERIFY(effects.actions.isEmpty());

    engine.OnStateUpdated(MakeState(true, false));
    QCOMPARE(effects.actions, QStringList{"pause"});

    engine.OnStateUpdated(MakeState(true, true, 15, 80, 80));
    QCOMPARE(effects.actions, (QStringList{"pause", "play"}));
    QCOMPARE(effects.notifications.size(), 1);
    QVERIFY(effects.notifications.first().contains("Left"));

    // A lost session starts over: no transition to react to, and the alert arms again.
    engine.OnDisconnected();
    engine.OnStateUpdated(MakeState(true, true, 15, 80, 80));
    QCOMPARE(effects.actions.size(), 2);
    QCOMPARE(effects.notifications.size(), 2);

    engine.OnDeviceConnectionChanged(false);
    QCOMPARE(effects.actions.last(), QString{"pause"});

    engine.SetAutoConnectOnLidOpen(true);
    engine.OnLidOpenedNearby();
    QCOMPARE(effects.connectRequests, 1);
    engine.OnLidOpenedNearby(); // inside the cooldown
    QCOMPARE(effects.connectRequests, 1);

    engine.OnDeviceConnectionChanged(true);
    engine.OnLidOpenedNearby(); // connected: nothing to do
    QCOMPARE(effects.connectRequests, 1);
}

void AutomationTests::BleWearJitterIsDebouncedAndPostDisconnectSuppressesLoop()
{
    using namespace std::chrono_literals;

    RecordingEffects effects;
    Engine engine{effects, nullptr, 80ms};
    engine.SetEarDetectionSingleBud(true);
    engine.SetSmartAudioRouting(true);

    engine.OnDeviceConnectionChanged(true);
    engine.OnStateUpdated(MakeState(true, false));
    QCOMPARE(effects.routeActions, QStringList{"airpods"});
    QVERIFY(effects.actions.isEmpty());

    // Brief BLE flicker to unworn and back within 80 ms must NOT pause or restore audio.
    engine.OnStateUpdated(MakeState(false, false));
    engine.OnStateUpdated(MakeState(true, false));
    QTest::qWait(120);
    QVERIFY(effects.actions.isEmpty());
    QCOMPARE(effects.routeActions, QStringList{"airpods"});

    // Sustained removal past the 80 ms debounce pauses and restores cleanly.
    engine.OnStateUpdated(MakeState(false, false));
    QTest::qWait(120);
    QCOMPARE(effects.actions, QStringList{"pause"});
    QCOMPARE(effects.routeActions, (QStringList{"airpods", "restore"}));

    // AutoConnect post-disconnect cooldown stops immediate reconnect loops.
    AutoConnectPolicy autoConn;
    const auto t0 = AutoConnectPolicy::Clock::now();
    autoConn.OnDeviceDisconnected(true, t0);
    QVERIFY(!autoConn.OnLidOpenedNearby(false, true, t0 + 3s));
    QVERIFY(
        autoConn.OnLidOpenedNearby(false, true, t0 + AutoConnectPolicy::kPostDisconnectCooldown));
}

QTEST_GUILESS_MAIN(AutomationTests)

#include "AutomationTests.moc"
