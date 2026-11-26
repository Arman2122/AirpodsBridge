// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <array>
#include <cstdint>
#include <map>
#include <vector>

#include <QtTest>
#include <QTemporaryFile>

#include <Config.h>
#include "Source/Domain/AirPodsAdvertisement.h"
#include "Source/Domain/AirPodsState.h"
#include "Source/Domain/AirPodsStateManager.h"
#include "Source/Domain/AppleCP.h"
#include "Source/Core/Settings.h"
#include "Source/Core/SettingsRepository.h"
#include "Source/Core/Update.h"
#include "Source/Presentation/MainWindowPresentation.h"
#include "Source/Gui/TaskbarGeometry.h"

namespace {

using Core::AirPods::Model;
using Core::AirPods::Side;
using Core::AirPods::Details::Advertisement;
using Core::AirPods::Details::StateManager;
using ReceivedData = Core::Bluetooth::ReceivedAdvertisement;

std::vector<uint8_t> MakePacket(
    uint16_t modelId, Side side, uint8_t leftBattery, uint8_t rightBattery, uint8_t caseBattery,
    bool bothInCase = true, bool lidOpened = true)
{
    std::vector<uint8_t> packet(27, 0);
    packet[0] = static_cast<uint8_t>(Core::AppleCP::PacketType::ProximityPairing);
    packet[1] = 25;
    packet[3] = static_cast<uint8_t>(modelId & 0xff);
    packet[4] = static_cast<uint8_t>(modelId >> 8);

    constexpr uint8_t kCurrentInEar = 1 << 1;
    constexpr uint8_t kBothInCase = 1 << 2;
    constexpr uint8_t kAnotherInEar = 1 << 3;
    constexpr uint8_t kBroadcastFromLeft = 1 << 5;
    packet[5] = kCurrentInEar | kAnotherInEar;
    if (bothInCase) {
        packet[5] |= kBothInCase;
    }
    if (side == Side::Left) {
        packet[5] |= kBroadcastFromLeft;
        packet[6] = static_cast<uint8_t>((rightBattery << 4) | leftBattery);
    }
    else {
        packet[6] = static_cast<uint8_t>((leftBattery << 4) | rightBattery);
    }

    packet[7] = caseBattery;
    packet[8] = lidOpened ? 0 : 1 << 3;
    return packet;
}

ReceivedData MakeAdvertisementData(
    uint64_t address, int16_t rssi, Side side, uint8_t leftBattery = 8, uint8_t rightBattery = 7,
    uint8_t caseBattery = 5, uint16_t modelId = 0x2014)
{
    ReceivedData data;
    data.address = address;
    data.rssi = rssi;
    data.manufacturerDataMap.emplace(
        Core::AppleCP::VendorId, MakePacket(modelId, side, leftBattery, rightBattery, caseBattery));
    return data;
}

} // namespace

class AirPodsDomainTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void RejectsMalformedPackets();
    void ParsesCompoundContinuityTlvPackets();
    void RecognizesAirPodsMaxUsbC();
    void RecognizesFullAirPodsAndBeatsCatalog();
    void ResolvesAirPodsMaxUsbCDisplayName();
    void ParsesAdvertisementState();
    void FiltersDuplicateAndWeakAdvertisements();
    void AcceptsOneStepBatteryChangeAcrossAddressRotation();
    void PreservesCaseFlagsFromLeftOnlyAdvertisement();
    void ExpiresStaleStateDeterministically();
    void MergesAdvertisementsFromBothSides();
    void RejectsAdvertisementsFromDifferentModels();
    void AcceptsKnownModelAfterUnknownAdvertisement();
    void LoadsSettingsThroughRepository();
    void PropagatesQuickConnectSettings();
    void PropagatesBehaviourOptions();
    void RejectsNullSettingsRepository();
    void ParsesUpdateVersions();
    void SanitizesUpdateAssetFileNames();
    void ParsesGitHubReleaseMetadata();
    void RejectsReleaseMetadataFromAnotherRepository();
    void RejectsUpdateAssetsWithoutDigest();
    void VerifiesUpdateFileDigest();
    void PresentsMainWindowLifecycleStates();
    void PresentsMainWindowConnectAndNoiseControl();
    void PresentsMainWindowNearbyOtherDevice();
    void PresentsMainWindowDeviceState();
    void PresentsMainWindowCaseBattery();
    void ReadsOnePieceBatteryFromEitherSide();
    void PresentsOnePieceDeviceAsOneBattery();
    void ConvertsTaskbarGeometryAcrossDpiBoundaries();
};

void AirPodsDomainTests::RejectsMalformedPackets()
{
    const std::vector<uint8_t> empty;
    QVERIFY(!Core::AppleCP::AirPods::IsValid(empty));

    auto packet = MakePacket(0x2014, Side::Left, 8, 7, 5);
    QVERIFY(Core::AppleCP::AirPods::IsValid(packet));

    packet[0] = static_cast<uint8_t>(Core::AppleCP::PacketType::AirDrop);
    QVERIFY(!Core::AppleCP::AirPods::IsValid(packet));

    packet = MakePacket(0x2014, Side::Left, 8, 7, 5);
    packet[1] = 24;
    QVERIFY(!Core::AppleCP::AirPods::IsValid(packet));

    // Truncated TLV record declaring more bytes than remain in the buffer.
    const std::vector<uint8_t> truncated{0x10, 0x20, 0x01, 0x02};
    QVERIFY(!Core::AppleCP::AirPods::IsValid(truncated));
}

void AirPodsDomainTests::ParsesCompoundContinuityTlvPackets()
{
    // NearbyInfo (0x10, len 5) followed by ProximityPairing (0x07, len 25).
    std::vector<uint8_t> compound{0x10, 0x05, 0x01, 0x02, 0x03, 0x04, 0x05};
    const auto proximity = MakePacket(0x2027, Side::Right, 9, 8, 6);
    compound.insert(compound.end(), proximity.begin(), proximity.end());

    const auto parsed = Core::AppleCP::AirPods::Parse(compound);
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->GetModel(), Model::AirPods_Pro_3);
    QCOMPARE(parsed->GetLeftBattery().Value(), 9U);
    QCOMPARE(parsed->GetRightBattery().Value(), 8U);
    QCOMPARE(parsed->GetCaseBattery().Value(), 6U);
}

void AirPodsDomainTests::RecognizesAirPodsMaxUsbC()
{
    QCOMPARE(Core::AppleCP::AirPods::GetModel(0x201F), Model::AirPods_Max_USB_C);
    QCOMPARE(Helper::ToString(Model::AirPods_Max_USB_C), QString{"AirPods Max (USB-C)"});
}

void AirPodsDomainTests::RecognizesFullAirPodsAndBeatsCatalog()
{
    using Core::AirPods::CapabilitiesFor;
    using Core::AirPods::ModelFromModelNumber;
    using Core::AirPods::ModelFromProductId;

    // 2025/2026 AirPods & Beats Bluetooth PIDs
    QCOMPARE(ModelFromProductId(0x2036), Model::AirPods_5);
    QCOMPARE(ModelFromProductId(0x2030), Model::AirPods_5_WCC);
    QCOMPARE(ModelFromProductId(0x202D), Model::AirPods_Max_2);
    QCOMPARE(ModelFromProductId(0x2003), Model::Powerbeats_3);
    QCOMPARE(ModelFromProductId(0x200D), Model::Powerbeats_4);
    QCOMPARE(ModelFromProductId(0x200B), Model::Powerbeats_Pro);
    QCOMPARE(ModelFromProductId(0x201D), Model::Powerbeats_Pro_2);
    QCOMPARE(ModelFromProductId(0x202F), Model::Powerbeats_Fit);
    QCOMPARE(ModelFromProductId(0x2005), Model::Beats_X);
    QCOMPARE(ModelFromProductId(0x2010), Model::Beats_Flex);
    QCOMPARE(ModelFromProductId(0x2006), Model::Beats_Solo3);
    QCOMPARE(ModelFromProductId(0x200C), Model::Beats_Solo_Pro);
    QCOMPARE(ModelFromProductId(0x2025), Model::Beats_Solo_4);
    QCOMPARE(ModelFromProductId(0x2026), Model::Beats_Solo_Buds);
    QCOMPARE(ModelFromProductId(0x2009), Model::Beats_Studio3);
    QCOMPARE(ModelFromProductId(0x2017), Model::Beats_Studio_Pro);
    QCOMPARE(ModelFromProductId(0x2011), Model::Beats_Studio_Buds);
    QCOMPARE(ModelFromProductId(0x2016), Model::Beats_Studio_Buds_Plus);
    QCOMPARE(ModelFromProductId(0x2038), Model::Beats_360);

    // Apple 0x1D hardware model-number resolution
    QCOMPARE(ModelFromModelNumber("A3531"), Model::AirPods_5);
    QCOMPARE(ModelFromModelNumber("A3439"), Model::AirPods_5_WCC);
    QCOMPARE(ModelFromModelNumber("A3454"), Model::AirPods_Max_2);
    QCOMPARE(ModelFromModelNumber("A3063"), Model::AirPods_Pro_3);
    QCOMPARE(ModelFromModelNumber("a3157 "), Model::Powerbeats_Pro_2);
    QCOMPARE(ModelFromModelNumber("A2924"), Model::Beats_Studio_Pro);
    QCOMPARE(ModelFromModelNumber("A3140"), Model::Beats_Solo_4);
    QCOMPARE(ModelFromModelNumber("A2871"), Model::Beats_Studio_Buds_Plus);
    QCOMPARE(ModelFromModelNumber("A9999"), Model::Unknown);

    // Key capabilities
    const auto pbp2 = CapabilitiesFor(Model::Powerbeats_Pro_2);
    QVERIFY(!pbp2.isOnePiece);
    QVERIFY(pbp2.supportsNoiseCancellation);
    QVERIFY(pbp2.supportsAdaptiveAudio);
    QVERIFY(pbp2.supportsHeartRate);

    const auto ap5 = CapabilitiesFor(Model::AirPods_5);
    QVERIFY(!ap5.isOnePiece);
    QVERIFY(ap5.supportsNoiseCancellation);
    QVERIFY(ap5.supportsAdaptiveAudio);
    QVERIFY(ap5.supportsAllowOffMode);

    const auto max2 = CapabilitiesFor(Model::AirPods_Max_2);
    QVERIFY(max2.isOnePiece);
    QVERIFY(max2.supportsAdaptiveAudio);
    QVERIFY(max2.supportsConversationalAwareness);

    const auto studioPro = CapabilitiesFor(Model::Beats_Studio_Pro);
    QVERIFY(studioPro.isOnePiece);
    QVERIFY(studioPro.supportsNoiseCancellation);
    QVERIFY(studioPro.supportsTransparency);

    const auto beats360 = CapabilitiesFor(Model::Beats_360);
    QVERIFY(beats360.isOnePiece);
    QVERIFY(beats360.supportsAdaptiveAudio);

    // Popup Heart Rate bar presentation honors setting toggle and model capability
    Gui::MainWindowViewModel vm;
    Core::AirPods::State hrState;
    hrState.model = Model::Powerbeats_Pro_2;
    hrState.displayName = QStringLiteral("Powerbeats Pro 2");
    hrState.pods.left.battery = 85;
    hrState.noiseControl.controllable = true;
    hrState.noiseControl.activeMode = Core::AirPods::NoiseControlMode::NoiseCancellation;
    hrState.noiseControl.heartRateMonitoringEnabled = true;
    hrState.noiseControl.heartRateStatus = Core::AirPods::HeartRateStatus::Live;
    hrState.noiseControl.heartRateBpm = 78;
    vm.UpdateState(hrState);

    auto pres = vm.Present();
    QVERIFY(pres.heartRate.visible);
    QCOMPARE(pres.heartRate.bpm, std::optional<uint16_t>{78});
    QVERIFY(!pres.statusBadge.contains("BPM"));

    vm.SetShowHeartRateInPopup(false);
    pres = vm.Present();
    QVERIFY(!pres.heartRate.visible);
    QVERIFY(pres.statusBadge.contains("78 BPM"));

    // Updating state before first battery frame arrives never shows a blank void.
    Core::AirPods::State earlyState;
    earlyState.model = Model::AirPods_Pro_3;
    earlyState.displayName = QStringLiteral("AirPods Pro");
    earlyState.noiseControl.controllable = true;
    vm.UpdateState(earlyState);
    const auto earlyPres = vm.Present();
    QVERIFY(!earlyPres.leftBattery.visible);
    QCOMPARE(earlyPres.statusBadge, QStringLiteral("Connected"));
}

void AirPodsDomainTests::ResolvesAirPodsMaxUsbCDisplayName()
{
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName("AirPods Max", Model::AirPods_Max_USB_C),
        QString{"AirPods Max (USB-C)"});
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName(
            "Studio AirPods Max - Find My", Model::AirPods_Max_USB_C),
        QString{"Studio AirPods Max"});
    QCOMPARE(
        Core::AirPods::Details::ResolveDisplayName("AirPods Max", Model::AirPods_Max),
        QString{"AirPods Max"});
}

void AirPodsDomainTests::ParsesAdvertisementState()
{
    const Advertisement advertisement{MakeAdvertisementData(0x1234, -45, Side::Left)};
    const auto &state = advertisement.GetAdvState();

    QCOMPARE(state.model, Model::AirPods_Pro_2);
    QCOMPARE(state.side, Side::Left);
    QCOMPARE(state.pods.left.battery.Value(), 80U);
    QCOMPARE(state.pods.right.battery.Value(), 70U);
    QCOMPARE(state.caseBox.battery.Value(), 50U);
    QVERIFY(state.pods.left.isInEar);
    QVERIFY(state.pods.right.isInEar);
    QVERIFY(state.caseBox.isBothPodsInCase);
    QVERIFY(state.caseBox.isLidOpened);
}

void AirPodsDomainTests::FiltersDuplicateAndWeakAdvertisements()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    auto first =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x1234, -45, Side::Left)});
    QVERIFY(first.has_value());
    QVERIFY(!first->oldState.has_value());

    auto duplicate =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x1234, -45, Side::Left)});
    QVERIFY(!duplicate.has_value());

    auto weak = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x1234, -90, Side::Left, 7, 7, 5)});
    QVERIFY(!weak.has_value());
    QCOMPARE(manager.GetCurrentState()->pods.left.battery.Value(), 80U);
}

void AirPodsDomainTests::AcceptsOneStepBatteryChangeAcrossAddressRotation()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    // Initial advertisement at 80% / 70% / 50% from address 0x1111.
    QVERIFY(
        manager
            .OnAdvReceived(Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left, 8, 7, 5)})
            .has_value());

    // Random address rotates to 0x2222 while left drops one 10% step (80% -> 70%).
    const auto rotated = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x2222, -48, Side::Left, 7, 7, 5)});
    QVERIFY(rotated.has_value());
    QCOMPARE(rotated->newState.pods.left.battery.Value(), 70U);

    // Another address with a 30% jump is likely a neighbor's device and must be rejected.
    const auto neighbor = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x3333, -49, Side::Left, 4, 7, 5)});
    QVERIFY(!neighbor.has_value());
    QCOMPARE(manager.GetCurrentState()->pods.left.battery.Value(), 70U);
}

void AirPodsDomainTests::PreservesCaseFlagsFromLeftOnlyAdvertisement()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    // Only the left bud broadcasts; case battery nibble is 0xF (unavailable), while lid is open.
    ReceivedData leftOnly;
    leftOnly.address = 0x1234;
    leftOnly.rssi = -45;
    leftOnly.manufacturerDataMap.emplace(
        Core::AppleCP::VendorId, MakePacket(0x2014, Side::Left, 8, 7, 0x0F, true, true));

    const auto update = manager.OnAdvReceived(Advertisement{leftOnly});
    QVERIFY(update.has_value());
    QVERIFY(!update->newState.caseBox.battery.Available());
    QVERIFY(update->newState.caseBox.isLidOpened);
    QVERIFY(update->newState.caseBox.isBothPodsInCase);
}

void AirPodsDomainTests::ExpiresStaleStateDeterministically()
{
    using namespace std::chrono_literals;

    StateManager manager;
    manager.OnRssiMinChanged(-80);
    bool discarded = false;
    manager.SetOnDiscardState([&] { discarded = true; });

    const auto t0 = StateManager::Clock::now();
    QVERIFY(!manager.NextExpiry(t0).has_value());

    QVERIFY(manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left)}, t0)
                .has_value());
    QVERIFY(manager.NextExpiry(t0).has_value());

    // Halfway through timeout: still alive.
    QVERIFY(!manager.ExpireStale(t0 + 5s));
    QVERIFY(!discarded);
    QVERIFY(manager.GetCurrentState().has_value());

    // Past timeout: state discarded and callback invoked.
    QVERIFY(manager.ExpireStale(t0 + StateManager::kStateTimeout));
    QVERIFY(discarded);
    QVERIFY(!manager.GetCurrentState().has_value());
    QVERIFY(!manager.NextExpiry(t0 + StateManager::kStateTimeout).has_value());
}

void AirPodsDomainTests::MergesAdvertisementsFromBothSides()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    QVERIFY(
        manager
            .OnAdvReceived(Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left, 8, 6, 4)})
            .has_value());

    auto update = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x2222, -46, Side::Right, 9, 7, 5)});
    QVERIFY(update.has_value());
    QVERIFY(update->oldState.has_value());
    QCOMPARE(update->newState.pods.left.battery.Value(), 90U);
    QCOMPARE(update->newState.pods.right.battery.Value(), 70U);
    QCOMPARE(update->newState.caseBox.battery.Value(), 50U);
}

void AirPodsDomainTests::RejectsAdvertisementsFromDifferentModels()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    const auto first =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left)});
    QVERIFY(first.has_value());
    QCOMPARE(first->newState.model, Model::AirPods_Pro_2);

    const auto differentModel = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x2222, -46, Side::Right, 8, 7, 5, 0x2013)});
    QVERIFY(!differentModel.has_value());
    QCOMPARE(manager.GetCurrentState()->model, Model::AirPods_Pro_2);
}

void AirPodsDomainTests::AcceptsKnownModelAfterUnknownAdvertisement()
{
    StateManager manager;
    manager.OnRssiMinChanged(-80);

    const auto unknown = manager.OnAdvReceived(
        Advertisement{MakeAdvertisementData(0x1111, -45, Side::Left, 8, 7, 5, 0xffff)});
    QVERIFY(unknown.has_value());
    QCOMPARE(unknown->newState.model, Model::Unknown);

    const auto known =
        manager.OnAdvReceived(Advertisement{MakeAdvertisementData(0x2222, -46, Side::Right)});
    QVERIFY(known.has_value());
    QCOMPARE(known->newState.model, Model::AirPods_Pro_2);
}

void AirPodsDomainTests::LoadsSettingsThroughRepository()
{
    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    repository->Write("auto_run", true);
    Core::Settings::SetRepository(std::move(repository));

    QCOMPARE(Core::Settings::Load(), Core::Settings::LoadResult::Successful);
    QVERIFY(Core::Settings::GetCurrent().auto_run);

    // `Apply` replays every field once, so a watcher registered before it starts from the
    // stored value rather than from its own default.
    int calls = 0;
    bool autoRun = false;
    const auto watch = Core::Settings::Watch(&Core::Settings::Fields::auto_run, [&](bool value) {
        autoRun = value;
        ++calls;
    });
    Core::Settings::Apply();
    QCOMPARE(calls, 1);
    QVERIFY(autoRun);

    Core::Settings::SetRepository(Core::Settings::CreatePersistentRepository());
}

void AirPodsDomainTests::PropagatesQuickConnectSettings()
{
    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    repository->Write("tray_quick_connect_enabled", true);
    repository->Write("tray_quick_connect_device_id", "{A}");
    Core::Settings::SetRepository(std::move(repository));

    QCOMPARE(Core::Settings::Load(), Core::Settings::LoadResult::Successful);

    bool enabled = false;
    int enabledCalls = 0;
    QString deviceId;
    int deviceCalls = 0;
    const auto enabledWatch =
        Core::Settings::Watch(&Core::Settings::Fields::tray_quick_connect_enabled, [&](bool value) {
            enabled = value;
            ++enabledCalls;
        });
    const auto deviceWatch = Core::Settings::Watch(
        &Core::Settings::Fields::tray_quick_connect_device_id, [&](const QString &value) {
            deviceId = value;
            ++deviceCalls;
        });
    Core::Settings::Apply();
    QVERIFY(enabled);
    QCOMPARE(enabledCalls, 1);
    QCOMPARE(deviceId, QString{"{A}"});
    QCOMPARE(deviceCalls, 1);

    Core::Settings::SetRepository(Core::Settings::CreatePersistentRepository());
}

void AirPodsDomainTests::PropagatesBehaviourOptions()
{
    // A store written by an older version has none of these keys, so every one of them
    // must fall back to the opt-in default rather than to `false`.
    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    Core::Settings::SetRepository(std::move(repository));

    QCOMPARE(Core::Settings::Load(), Core::Settings::LoadResult::Successful);

    const auto defaults = Core::Settings::GetCurrent();
    QVERIFY(defaults.check_for_updates);
    QVERIFY(defaults.show_popup_on_lid_open);
    QVERIFY(defaults.show_nearby_popup_when_disconnected);
    QVERIFY(defaults.ear_detection_resume_playback);

    bool checkForUpdates = false;
    int checkCalls = 0;
    int popupCalls = 0;
    auto checkWatch =
        Core::Settings::Watch(&Core::Settings::Fields::check_for_updates, [&](bool value) {
            checkForUpdates = value;
            ++checkCalls;
        });
    auto popupWatch = Core::Settings::Watch(
        &Core::Settings::Fields::show_popup_on_lid_open, [&](bool) { ++popupCalls; });
    Core::Settings::Apply();
    QVERIFY(checkForUpdates);
    QCOMPARE(checkCalls, 1);
    QCOMPARE(popupCalls, 1);

    // A change through the accessor notifies only the field that changed ...
    Core::Settings::ModifiableAccess()->check_for_updates = false;
    QVERIFY(!checkForUpdates);
    QCOMPARE(checkCalls, 2);
    QCOMPARE(popupCalls, 1);

    // ... and writing the value that is already stored is not a change.
    Core::Settings::ModifiableAccess()->check_for_updates = false;
    QCOMPARE(checkCalls, 2);

    // A watcher runs after the settings lock is released, so it may read settings itself.
    bool sawStoredValue = false;
    auto reentrantWatch =
        Core::Settings::Watch(&Core::Settings::Fields::show_popup_on_lid_open, [&](bool value) {
            sawStoredValue = Core::Settings::GetCurrent().show_popup_on_lid_open == value;
        });
    Core::Settings::ModifiableAccess()->show_popup_on_lid_open = false;
    QVERIFY(sawStoredValue);
    QCOMPARE(popupCalls, 2);

    // A subscription ends with its handle.
    reentrantWatch.Reset();
    sawStoredValue = false;
    Core::Settings::ModifiableAccess()->show_popup_on_lid_open = true;
    QVERIFY(!sawStoredValue);
    QCOMPARE(popupCalls, 3);

    Core::Settings::SetRepository(Core::Settings::CreatePersistentRepository());
}

void AirPodsDomainTests::RejectsNullSettingsRepository()
{
    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    repository->Write("auto_run", true);
    Core::Settings::SetRepository(std::move(repository));

    Core::Settings::SetRepository(nullptr);

    QCOMPARE(Core::Settings::Load(), Core::Settings::LoadResult::Successful);
    QVERIFY(Core::Settings::GetCurrent().auto_run);
    Core::Settings::SetRepository(Core::Settings::CreatePersistentRepository());
}

void AirPodsDomainTests::ParsesUpdateVersions()
{
    QCOMPARE(Core::Update::ToVersionNumber("v0.5.0"), QVersionNumber(0, 5, 0));
    QCOMPARE(Core::Update::ToVersionNumber("0.5.0"), QVersionNumber(0, 5, 0));
}

void AirPodsDomainTests::SanitizesUpdateAssetFileNames()
{
    QCOMPARE(
        Core::Update::Details::SanitizeAssetFileName("AirpodsBridge-1.1.0-win32.exe"),
        QString{"AirpodsBridge-1.1.0-win32.exe"});
    QCOMPARE(
        Core::Update::Details::SanitizeAssetFileName("../../evil/AirpodsBridge-1.1.0-win32.exe"),
        QString{"AirpodsBridge-1.1.0-win32.exe"});
    QCOMPARE(Core::Update::Details::SanitizeAssetFileName("..\\..\\evil.exe"), QString{"evil.exe"});
    QVERIFY(Core::Update::Details::SanitizeAssetFileName("..").isEmpty());
    QVERIFY(Core::Update::Details::SanitizeAssetFileName(".").isEmpty());
}

void AirPodsDomainTests::ParsesGitHubReleaseMetadata()
{
    const auto metadata = QString{R"json({
        "tag_name": "v0.5.0",
        "body": "## Change log\n- Add automatic updates\n\nInstallation notes",
        "html_url": "%1/tag/v0.5.0",
        "prerelease": false,
        "assets": [{
            "name": "AirpodsBridge-0.5.0-win32.exe",
            "size": 123456,
            "digest": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            "browser_download_url": "%1/download/v0.5.0/AirpodsBridge-0.5.0-win32.exe"
        }]
    })json"}
                              .arg(Config::UrlReleases);
    const auto release = Core::Update::Details::ParseSingleReleaseResponse(metadata.toStdString());

    QVERIFY(release.has_value());
    QCOMPARE(release->version, QVersionNumber(0, 5, 0));
    QCOMPARE(release->fileName, QString{"AirpodsBridge-0.5.0-win32.exe"});
    QCOMPARE(release->fileSize, size_t{123456});
    QCOMPARE(
        release->sha256,
        QString{"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"});
    QCOMPARE(release->changeLog, QString{"- Add automatic updates"});
    QVERIFY(release->CanAutoUpdate());
    QVERIFY(!release->isPreRelease);

    const auto generatedNotes = QString{R"json({
        "tag_name": "v0.5.0",
        "body": "## What's Changed\n* Improve update handling\n\n**Full Changelog**: https://example.invalid/compare",
        "html_url": "%1/tag/v0.5.0",
        "prerelease": false,
        "assets": []
    })json"}
                                    .arg(Config::UrlReleases);
    const auto generatedRelease =
        Core::Update::Details::ParseSingleReleaseResponse(generatedNotes.toStdString());

    QVERIFY(generatedRelease.has_value());
    QCOMPARE(generatedRelease->changeLog, QString{"* Improve update handling"});

    // A hand-written release body: CRLF line endings, a preamble before the heading, and
    // an emoji shortcode between the hashes and the words.
    const auto decorated = QString{R"json({
        "tag_name": "1.1.0",
        "body": "Beta notice\r\n\r\n## :scroll: Change log\r\n1. Supported AirPods 4\r\n\r\nSorry",
        "html_url": "%1/tag/1.1.0",
        "prerelease": false,
        "assets": []
    })json"}
                               .arg(Config::UrlReleases);
    const auto decoratedRelease =
        Core::Update::Details::ParseSingleReleaseResponse(decorated.toStdString());

    QVERIFY(decoratedRelease.has_value());
    QCOMPARE(decoratedRelease->changeLog, QString{"1. Supported AirPods 4"});
}

void AirPodsDomainTests::RejectsReleaseMetadataFromAnotherRepository()
{
    const auto release = Core::Update::Details::ParseSingleReleaseResponse(R"json({
        "tag_name": "v9.9.9",
        "body": "Change log\nUntrusted release",
        "html_url": "https://github.com/AnotherOwner/AirpodsBridge/releases/tag/v9.9.9",
        "prerelease": false,
        "assets": []
    })json");

    QVERIFY(!release.has_value());
}

void AirPodsDomainTests::RejectsUpdateAssetsWithoutDigest()
{
    const auto metadata = QString{R"json({
        "tag_name": "v0.5.0",
        "body": "Change log\nUnsigned asset",
        "html_url": "%1/tag/v0.5.0",
        "prerelease": false,
        "assets": [{
            "name": "AirpodsBridge-0.5.0-win32.exe",
            "size": 123456,
            "browser_download_url": "%1/download/v0.5.0/AirpodsBridge-0.5.0-win32.exe"
        }]
    })json"}
                              .arg(Config::UrlReleases);
    const auto release = Core::Update::Details::ParseSingleReleaseResponse(metadata.toStdString());

    QVERIFY(release.has_value());
    QVERIFY(!release->CanAutoUpdate());
}

void AirPodsDomainTests::VerifiesUpdateFileDigest()
{
    QTemporaryFile file;
    QVERIFY(file.open());
    QCOMPARE(file.write("test"), qint64{4});
    file.close();

    const QString expected = "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08";
    QVERIFY(Core::Update::Details::VerifyFileSha256(file.fileName(), expected));
    QVERIFY(!Core::Update::Details::VerifyFileSha256(
        file.fileName(), "0000000000000000000000000000000000000000000000000000000000000000"));
}

void AirPodsDomainTests::PresentsMainWindowLifecycleStates()
{
    Gui::MainWindowViewModel viewModel;

    auto presentation = viewModel.Present();
    QCOMPARE(presentation.title, QString{"Unavailable"});
    QCOMPARE(presentation.buttonAction, Gui::ButtonAction::NoButton);
    QVERIFY(!presentation.artwork.has_value());

    viewModel.Available();
    QCOMPARE(viewModel.Present().title, QString{"Disconnected"});

    viewModel.Unbind();
    presentation = viewModel.Present();
    QCOMPARE(presentation.title, QString{"Waiting for Binding"});
    QCOMPARE(presentation.buttonAction, Gui::ButtonAction::Bind);

    viewModel.Disconnect();
    QCOMPARE(viewModel.Present(), presentation);

    viewModel.Bind();
    QCOMPARE(viewModel.Present().title, QString{"Disconnected"});
}

void AirPodsDomainTests::PresentsMainWindowConnectAndNoiseControl()
{
    Gui::MainWindowViewModel viewModel;
    viewModel.Available();
    viewModel.Bind();
    viewModel.SetQuickConnectAvailable(true);

    // Bound and disconnected with Quick Connect ready shows a Connect button.
    auto presentation = viewModel.Present();
    QCOMPARE(presentation.title, QString{"Disconnected"});
    QCOMPARE(presentation.buttonAction, Gui::ButtonAction::Connect);
    QVERIFY(!presentation.noiseControl.visible);

    // BLE-only connected AirPods Pro 3: no noise-control bar yet.
    Core::AirPods::State state;
    state.displayName = "AirPods Pro 3";
    state.model = Model::AirPods_Pro_3;
    state.pods.left.battery = 80;
    state.pods.right.battery = 75;
    state.estimatedMinutesRemaining = 158;
    viewModel.UpdateState(state);
    presentation = viewModel.Present();
    QCOMPARE(presentation.buttonAction, Gui::ButtonAction::NoButton);
    QVERIFY(!presentation.noiseControl.visible);
    QVERIFY(presentation.statusBadge.isEmpty());

    // AAP-connected AirPods Pro 3: reveals Noise Control with Adaptive support + quick pills.
    state.noiseControl.controllable = true;
    state.noiseControl.activeMode = Core::AirPods::NoiseControlMode::NoiseCancellation;
    state.noiseControl.conversationalAwareness = true;
    state.noiseControl.conversationalAwarenessSpeaking = true;
    state.noiseControl.hearingAidEnabled = true;
    state.noiseControl.heartRateBpm = 74;
    state.audioProfile = Core::AirPods::AudioProfile::HandsFree;
    viewModel.UpdateState(state);
    presentation = viewModel.Present();
    QVERIFY(presentation.noiseControl.visible);
    QCOMPARE(
        presentation.noiseControl.activeMode, Core::AirPods::NoiseControlMode::NoiseCancellation);
    QVERIFY(presentation.noiseControl.supportsAdaptive);
    QVERIFY(presentation.noiseControl.supportsConversationalAwareness);
    QVERIFY(presentation.noiseControl.conversationalAwareness);
    QVERIFY(presentation.noiseControl.conversationalAwarenessSpeaking);
    QVERIFY(presentation.noiseControl.supportsHearingAid);
    QVERIFY(presentation.noiseControl.hearingAidEnabled);
    QCOMPARE(presentation.noiseControl.heartRateBpm.value_or(0), uint16_t{74});
    QVERIFY(presentation.heartRate.visible);
    QCOMPARE(presentation.heartRate.bpm.value_or(0), uint16_t{74});
    QCOMPARE(presentation.statusBadge, QStringLiteral("Hands-Free Audio"));

    // Turning off the dedicated popup Heart Rate bar moves BPM into the status badge.
    viewModel.SetShowHeartRateInPopup(false);
    presentation = viewModel.Present();
    QVERIFY(!presentation.heartRate.visible);
    QCOMPARE(
        presentation.statusBadge,
        QStringLiteral("%1 74 BPM %2 Hands-Free Audio").arg(QChar{0x2665}, QChar{0x00B7}));
    viewModel.SetShowHeartRateInPopup(true);

    // Non-ANC model (AirPods 2) never shows noise control even if transport flags controllable.
    state.model = Model::AirPods_2;
    viewModel.UpdateState(state);
    QVERIFY(!viewModel.Present().noiseControl.visible);
}

void AirPodsDomainTests::PresentsMainWindowNearbyOtherDevice()
{
    Gui::MainWindowViewModel viewModel;
    viewModel.Available();
    viewModel.Bind();
    viewModel.SetQuickConnectAvailable(true);

    Core::AirPods::State nearby;
    nearby.displayName = QStringLiteral("AirPods Pro 3");
    nearby.model = Model::AirPods_Pro_3;
    nearby.isNearbyOtherDevice = true;
    nearby.pods.left.battery = 90;
    nearby.pods.right.battery = 85;
    nearby.caseBox.battery = 60;
    nearby.caseBox.isLidOpened = false;

    viewModel.UpdateNearbyState(nearby);
    auto presentation = viewModel.Present();
    QVERIFY(presentation.isNearby);
    QCOMPARE(presentation.title, QStringLiteral("AirPods Pro 3"));
    QCOMPARE(presentation.statusBadge, QStringLiteral("Connected to another device"));
    QCOMPARE(presentation.buttonAction, Gui::ButtonAction::Connect);
    QVERIFY(presentation.artwork.has_value());
    QVERIFY(presentation.leftBattery.visible);
    QCOMPARE(presentation.leftBattery.value, 90U);
    QVERIFY(presentation.rightBattery.visible);
    QCOMPARE(presentation.rightBattery.value, 85U);
    QVERIFY(presentation.caseBattery.visible);
    QCOMPARE(presentation.caseBattery.value, 60U);

    // Lid-open nearby broadcast updates the badge cleanly.
    nearby.caseBox.isLidOpened = true;
    viewModel.UpdateNearbyState(nearby);
    QCOMPARE(viewModel.Present().statusBadge, QStringLiteral("Nearby \u00B7 Case open"));

    // Connecting indicator switches button state until connected or cleared.
    viewModel.SetConnecting(true);
    QVERIFY(viewModel.Present().connecting);
    viewModel.SetConnecting(false);
    QVERIFY(!viewModel.Present().connecting);

    // Inline transient status shows cleanly without requiring a Windows notification.
    viewModel.SetTransientStatus(QStringLiteral("Open case to connect"));
    QCOMPARE(viewModel.Present().statusBadge, QStringLiteral("Open case to connect"));
    viewModel.SetTransientStatus({});

    // Controllable state without any battery or mode reading must not show empty ANC controls.
    Gui::MainWindowViewModel emptyVm;
    Core::AirPods::State emptyState;
    emptyState.model = Model::AirPods_Pro_3;
    emptyState.noiseControl.controllable = true;
    emptyVm.UpdateState(emptyState);
    QVERIFY(!emptyVm.Present().noiseControl.visible);

    // Closed case with both buds inside uses the dedicated in-case badge.
    nearby.caseBox.isLidOpened = false;
    nearby.caseBox.isBothPodsInCase = true;
    viewModel.UpdateNearbyState(nearby);
    QCOMPARE(viewModel.Present().statusBadge, QStringLiteral("In case \u00B7 Nearby"));

    // Lid-open counter low 3 bits decode accurately from Continuity byte 8.
    auto pkt = MakePacket(0x2027, Side::Left, 9, 8, 6, true, true);
    pkt[8] = 0x05; // lid open (bit 3 == 0), counter == 5
    const auto parsed = Core::AppleCP::AirPods::Parse(pkt);
    QVERIFY(parsed.has_value());
    QVERIFY(parsed->IsLidOpened());
    QCOMPARE(parsed->GetLidOpenCounter(), uint8_t{5});

    // Active connected state takes priority and cannot be clobbered by a nearby broadcast.
    Core::AirPods::State connected = nearby;
    connected.isNearbyOtherDevice = false;
    viewModel.UpdateState(connected);
    QVERIFY(!viewModel.Present().isNearby);

    nearby.pods.left.battery = 10;
    viewModel.UpdateNearbyState(nearby);
    QVERIFY(!viewModel.Present().isNearby);
    QCOMPARE(viewModel.Present().leftBattery.value, 90U);

    // Clearing nearby after disconnect returns cleanly to Disconnected.
    viewModel.Disconnect();
    viewModel.UpdateNearbyState(nearby);
    QVERIFY(viewModel.Present().isNearby);
    viewModel.ClearNearby();
    QVERIFY(!viewModel.Present().isNearby);
    QCOMPARE(viewModel.Present().buttonAction, Gui::ButtonAction::Connect);
}

void AirPodsDomainTests::PresentsMainWindowDeviceState()
{
    Core::AirPods::State state;
    state.displayName = "Office AirPods";
    state.model = Core::AirPods::Model::AirPods_Pro_2;
    state.pods.left.battery = 80;
    state.pods.left.isCharging = true;
    state.pods.right.battery = 60;

    Gui::MainWindowViewModel viewModel;
    viewModel.UpdateState(state);
    const auto presentation = viewModel.Present();

    QCOMPARE(presentation.title, state.displayName);
    QVERIFY(presentation.artwork.has_value());
    QVERIFY(presentation.artwork->still.startsWith(":/Resource/Device/AirPodsPro1_2/"));
    QVERIFY(presentation.leftBattery.visible);
    QVERIFY(presentation.leftBattery.charging);
    QCOMPARE(presentation.leftBattery.value, 80U);
    QVERIFY(presentation.rightBattery.visible);
    QCOMPARE(presentation.rightBattery.value, 60U);
    QVERIFY(!presentation.caseBattery.visible);
}

void AirPodsDomainTests::PresentsMainWindowCaseBattery()
{
    // Mirrors `PresentsMainWindowDeviceState`, which reports the pods but not the case.
    // Reporting only the case pins each slot to its own source.
    Core::AirPods::State state;
    state.displayName = "Office AirPods";
    state.model = Core::AirPods::Model::AirPods_4_ANC;
    state.caseBox.battery = 40;
    state.caseBox.isCharging = true;

    Gui::MainWindowViewModel viewModel;
    viewModel.UpdateState(state);
    auto presentation = viewModel.Present();

    QVERIFY(presentation.caseBattery.visible);
    QVERIFY(presentation.caseBattery.charging);
    QCOMPARE(presentation.caseBattery.value, 40U);
    QVERIFY(!presentation.leftBattery.visible);
    QVERIFY(!presentation.rightBattery.visible);

    state.caseBox.isCharging = false;
    viewModel.UpdateState(state);
    presentation = viewModel.Present();

    QVERIFY(presentation.caseBattery.visible);
    QVERIFY(!presentation.caseBattery.charging);
}

void AirPodsDomainTests::ReadsOnePieceBatteryFromEitherSide()
{
    using Core::AirPods::IsOnePiece;
    using Core::AirPods::Model;
    using Core::AirPods::OnePieceState;

    QVERIFY(IsOnePiece(Model::AirPods_Max));
    QVERIFY(IsOnePiece(Model::AirPods_Max_USB_C));
    QVERIFY(IsOnePiece(Model::Beats_Solo3));
    QVERIFY(!IsOnePiece(Model::AirPods_Pro_3));
    QVERIFY(!IsOnePiece(Model::Beats_Fit_Pro));
    QVERIFY(!IsOnePiece(Model::Unknown));

    // The level may arrive on either side; the other side reports nothing.
    Core::AirPods::PodsState pods;
    pods.left.battery = 70;
    pods.left.isCharging = true;
    QCOMPARE(OnePieceState(pods).battery.Value(), 70U);
    QVERIFY(OnePieceState(pods).isCharging);

    pods = {};
    pods.right.battery = 35;
    QCOMPARE(OnePieceState(pods).battery.Value(), 35U);
    QVERIFY(!OnePieceState(pods).isCharging);

    // Mirrored on both, not quite equal: the lower level, charging if either says so.
    pods.left.battery = 40;
    pods.right.isCharging = true;
    QCOMPARE(OnePieceState(pods).battery.Value(), 35U);
    QVERIFY(OnePieceState(pods).isCharging);

    QVERIFY(!OnePieceState({}).battery.Available());
}

void AirPodsDomainTests::PresentsOnePieceDeviceAsOneBattery()
{
    // AirPods Max: one battery, no case. Whatever the pair's layout carries, the popup
    // shows one ring, in the left slot, and nothing else.
    Core::AirPods::State state;
    state.displayName = "AirPods Max";
    state.model = Core::AirPods::Model::AirPods_Max;
    state.pods.right.battery = 55;
    state.pods.right.isCharging = true;
    state.caseBox.battery = 100; // a stray value; a one-piece device has no case

    Gui::MainWindowViewModel viewModel;
    viewModel.UpdateState(state);
    const auto presentation = viewModel.Present();

    QVERIFY(presentation.leftBattery.visible);
    QVERIFY(presentation.leftBattery.charging);
    QCOMPARE(presentation.leftBattery.value, 55U);
    QVERIFY(!presentation.rightBattery.visible);
    QVERIFY(!presentation.caseBattery.visible);

    // And the artwork agrees: one object, one anchor to sit the ring under.
    QVERIFY(presentation.artwork.has_value());
    QCOMPARE(presentation.artwork->anchors.size(), size_t{1});
}

void AirPodsDomainTests::ConvertsTaskbarGeometryAcrossDpiBoundaries()
{
    // Reproduces issue #217: one 2560x1440 monitor at 125% display scaling. Win32 reports
    // physical pixels, while QWidget geometry is expressed in device-independent pixels.
    const auto layout =
        Gui::TaskbarGeometry::CalculateLayout(QSize{2560, 50}, QRect{160, 0, 2325, 50}, 120, true);

    QCOMPARE(layout.statusLogical, QRect(1988, 0, 60, 40));
    QCOMPARE(layout.taskButtonsNative, QRect(160, 0, 2325, 50));

    QCOMPARE(Gui::TaskbarGeometry::LogicalToNative(layout.statusLogical.right() + 1, 120), 2560);

    const auto restored =
        Gui::TaskbarGeometry::RestoreTaskButtons(QSize{2560, 50}, layout.taskButtonsNative, true);
    QCOMPARE(restored, QRect(160, 0, 2400, 50));

    const auto vertical =
        Gui::TaskbarGeometry::CalculateLayout(QSize{50, 1440}, QRect{0, 120, 50, 1270}, 120, false);
    QCOMPARE(vertical.statusLogical, QRect(0, 1112, 40, 40));
    QCOMPARE(vertical.taskButtonsNative, QRect(0, 120, 50, 1270));
}

QTEST_GUILESS_MAIN(AirPodsDomainTests)

#include "AirPodsDomainTests.moc"
