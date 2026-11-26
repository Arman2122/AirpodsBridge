// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <unordered_set>
#include <vector>

#include <QObject>
#include <QTimer>

#include "Source/Core/AirPodsTransport.h"
#include "Source/Core/Bluetooth.h"
#include "Source/Domain/AirPodsStateManager.h"
#include "Source/Domain/RtBuddySensor.h"

namespace Core::AirPods {

//
// Owns the platform-facing half of the feature: it drives the BLE scanner, tracks the
// bound device's connection state, optionally bridges an AAP `ControlTransport`, and feeds
// updates to the domain state manager.
//
// It reports what the device is doing and decides nothing about it. What to do in
// response -- pause media, connect, notify -- is `Core::Automation::Engine`, listening to
// the signals below.
//
class Manager : public QObject
{
    Q_OBJECT

public:
    explicit Manager(
        QObject *parent = nullptr, std::unique_ptr<ControlTransport> controlTransport =
                                       std::make_unique<NullControlTransport>());
    ~Manager();

    void StartScanner();
    void StopScanner();

    void OnRssiMinChanged(int16_t rssiMin);
    void OnBoundDeviceAddressChanged(uint64_t address);

    // Keep the scanner running while the bound device is disconnected, so that its case
    // opening nearby is noticed. This costs idle CPU; see Docs/Performance.md.
    void SetScanWhileDisconnected(bool enable);
    void SetShowNearbyWhenDisconnected(bool enable);
    void SetShowPopupOnLidOpen(bool enable);

    // Future-proof two-way control actions (active whenever an AAP ControlTransport is
    // connected; safe no-ops on BLE-only sessions).
    bool SetNoiseControlMode(NoiseControlMode mode);
    bool CycleNoiseControlMode();
    bool SetConversationalAwareness(bool enabled);
    bool SetOneBudAnc(bool enabled);
    bool SetPersonalizedVolume(bool enabled);
    bool SetLoudSoundReduction(bool enabled);
    bool SetAdaptiveNoiseLevel(uint8_t level);
    bool SetHeadGestures(bool enabled);
    bool SetMicrophoneSide(MicrophoneSide side);
    bool SetToneVolume(uint8_t volumePercent);
    bool SetAllowOffListeningMode(bool allow);
    bool SetListeningModeCycle(uint8_t modeMask);
    bool SetPressSpeed(PressSpeed speed);
    bool SetPressHoldDuration(PressHoldDuration duration);
    bool SetHearingAid(bool enabled);
    bool SetMediaAssist(bool enabled);
    bool SetHeartRateMonitoring(bool enabled);
    bool RenameDevice(const QString &name);
    void SetAudioProfile(AudioProfile profile, QString sourceProcess = {});
    void SetLowLatencyMode(bool enabled);

    // No-Test-Mode Radio Service controls & status.
    std::optional<RadioServiceInfo> GetRadioServiceInfo() const;
    bool ApplyRadioProxyOptions(const RadioProxyOptions &options);
    bool RetryRadioServiceAfterGuard();
    bool RestoreNativeRadioDriver();
    void ReconnectControlTransport();
    void DisconnectControlTransport();
    void RebindAfterRadioChange();

    struct DiagnosticsSnapshot {
        QString boundDeviceName;
        Model boundModel{Model::Unknown};
        bool bluetoothConnected{false};
        bool aapDriverAvailable{false};
        bool aapConnected{false};
        QString transportBackend;
        std::optional<RadioServiceInfo> radioServiceInfo;
        bool scannerRunning{false};
        std::optional<int16_t> lastRssi;
        QString lastAdvHex;
        std::optional<uint8_t> heartRateServiceId;
        std::optional<State> currentState;
    };

    DiagnosticsSnapshot GetDiagnostics() const;

    // Power lifecycle hooks (`WM_POWERBROADCAST`).
    void OnSystemSuspend();
    void OnSystemResume();

private:
    mutable std::mutex _mutex;
    Details::StateManager _stateMgr;
    std::unique_ptr<ControlTransport> _controlTransport;
    std::optional<Bluetooth::Device> _boundDevice;
    QString _deviceName;
    QString _boundContainerId;
    Model _boundModel{Model::Unknown};
    bool _deviceConnected{false};
    bool _aapConnected{false};
    bool _systemSuspended{false};
    bool _scannerWanted{false};
    bool _scanWhileDisconnected{false};
    bool _showNearbyWhenDisconnected{true};
    bool _showPopupOnLidOpen{true};
    std::optional<State> _lastNearbyState;
    std::chrono::steady_clock::time_point _lastNearbySeen{};
    bool _lastNearbyLidOpen{false};
    std::optional<uint8_t> _lastNearbyLidCounter;
    std::optional<int16_t> _lastRssi;
    QString _lastAdvHex;
    std::unordered_set<uint8_t> _seenUnknownAapOpcodes;
    // RTBuddy optical heart-rate session state (AirPods Pro 3, AAP opcode 0x17).
    RtBuddy::FrameReassembler _rtBuddyReassembler;
    RtBuddy::SessionParser _rtBuddyParser;
    uint8_t _rtBuddySeq{0};
    bool _heartRateWanted{false};
    bool _heartRatePausedOutOfEar{false};
    bool _heartRateWarmupLogged{false};
    uint64_t _heartRateEpoch{0};
    int _heartRateAttempt{0};
    std::chrono::steady_clock::time_point _lastHeartRateSampleTime{};
    std::optional<uint16_t> _lastLoggedBpm;
    // AAP handshake & listening-mode settle state (prevents Adaptive->Off and Off->Previous
    // bounces).
    bool _aapFeaturesEnabledSent{false};
    bool _allowOffSentToDevice{false};
    bool _userDisabledAllowOff{false};
    uint64_t _pendingOffModeEpoch{0};
    std::chrono::steady_clock::time_point _pendingOffModeUntil{};
    std::chrono::steady_clock::time_point _pendingOffModeSendAt{};
    bool _offModeRetrySent{false};
    uint64_t _pendingAdaptiveEpoch{0};
    std::chrono::steady_clock::time_point _pendingAdaptiveUntil{};
    std::chrono::steady_clock::time_point _adaptiveRequestedAt{};
    bool _adaptiveRetrySent{false};
    uint64_t _adaptiveLevelEpoch{0};
    // Mirrors the state manager's threshold for the disconnected path, which never reaches
    // the state manager. Rejects everything until the setting arrives.
    int16_t _rssiMin{std::numeric_limits<int16_t>::max()};
    uint64_t _requestedDeviceAddress{0};
    std::jthread _deviceLookupThread;
    QTimer _expiryTimer{this};
    QTimer _heartRateWatchdog{this};
    QTimer _convSpeechSafetyTimer{this};

    // Declared last so that it is destroyed first: its destructor stops the watcher and waits
    // for in-flight callbacks, which reach `_stateMgr` and `_mutex` above.
    Bluetooth::AdvertisementWatcher _adWatcher;

    struct PendingStateSignals {
        std::optional<State> state;
        std::optional<bool> lidToggled;
    };

    struct PendingNearbySignals {
        bool lidOpenedNearby{false};
        std::optional<State> nearbyState;
        bool shouldAutoShow{false};
        bool scheduleExpiry{false};
    };

    bool ShouldScan() const;
    void ReconcileScannerLater();
    void ScheduleExpiryTimerLater();
    void OnExpiryTimeout();
    void CompleteBoundDeviceLookup(uint64_t address, std::optional<Bluetooth::Device> device);
    void HandleBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state);
    void OnControlConnectionChanged(bool connected);
    void CompleteAapPostHandshake(uint64_t epoch, bool resumeHeartRate);
    void OnControlPacketReceived(std::span<const uint8_t> packet);
    void StartHeartRateCampaignLater(bool resetAttempts);
    void RunHeartRateAttempt(uint64_t epoch, int attempt);
    void StopHeartRateWire();
    void OnHeartRateWatchdogTimeout();
    void UpdateHeartRateStatus(HeartRateStatus status);
    void ArmConversationSpeechSafetyLater(uint8_t level);
    void OnConversationSpeechSafetyTimeout();
    uint8_t NextRtBuddySeqLocked();
    bool SendAapControl(
        std::vector<uint8_t> packet,
        std::optional<Aap::InboundMessage> optimisticMsg = std::nullopt,
        std::vector<uint8_t> followUpPacket = {});
    PendingStateSignals PrepareStateChangedLocked(Details::StateManager::UpdateEvent updateEvent);
    void EmitStateSignals(const PendingStateSignals &pending);
    bool OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data);
    PendingNearbySignals OnAdvertisementWhileDisconnectedLocked(const Details::Advertisement &adv);
    void OnAdvWatcherStateChanged(
        Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError);

Q_SIGNALS:
    void StateUpdated(const Core::AirPods::State &newState);
    void BoundDeviceResolved(
        const QString &displayName, Core::AirPods::Model model, const QString &containerId);
    void BoundDeviceUnavailable();
    void Disconnected();
    void LidToggled(bool opened);
    // The bound model's case opened within range while the device is not connected. Only
    // reported while scanning is kept on for that purpose, and repeats with every
    // advertisement the open case sends.
    void LidOpenedNearby();
    // AirPods broadcast nearby while not connected to this PC (e.g. connected to an iPhone).
    // `shouldAutoShow` is true on fresh appearance or lid-open transition.
    void NearbyStateUpdated(const Core::AirPods::State &nearbyState, bool shouldAutoShow);
    void NearbyCleared();
    void ScannerAvailabilityChanged(bool available);
    void DeviceConnectionChanged(bool connected);
    void ControlConnectionChanged(bool connected);
    // Raw AAP 0x4B Conversation Awareness ducking level (0 = restore/off, 1..2 = speaking,
    // 3..7 = gradual recovery, 8..9 = normal).
    void ConversationAwarenessLevelChanged(uint8_t level);
};

std::vector<Core::Bluetooth::Device> GetDevices();

} // namespace Core::AirPods
