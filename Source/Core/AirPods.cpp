// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/AirPods.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <utility>

#include "Source/Domain/AapProtocol.h"
#include "Source/Support/Error.h"
#include "Source/Support/Helper.h"
#include "Source/Support/Logger.h"

namespace Core::AirPods {

namespace {

void LogDecodedAapMessage(const Aap::InboundMessage &msg)
{
    std::visit(
        Helper::Overloaded{
            [](const Aap::BatteryNotification &b) {
                std::string summary;
                for (const auto &e : b.entries) {
                    if (!summary.empty()) {
                        summary += ", ";
                    }
                    const char *comp = e.component == Aap::ComponentType::Left    ? "L"
                                       : e.component == Aap::ComponentType::Right ? "R"
                                       : e.component == Aap::ComponentType::Case  ? "Case"
                                                                                  : "Single";
                    if (e.status == Aap::ChargeStatus::Disconnected) {
                        summary += std::format("{}=off", comp);
                    }
                    else {
                        summary += std::format(
                            "{}={}%{}", comp, e.level,
                            e.status == Aap::ChargeStatus::Charging ? "+" : "");
                    }
                }
                LOG(Info, "AAP exact battery (1%): {}", summary);
            },
            [](const Aap::EarDetectionNotification &e) {
                LOG(Info, "AAP ear detection: primary={}, secondary={}",
                    static_cast<int>(e.primary), static_cast<int>(e.secondary));
            },
            [](const Aap::NoiseControlNotification &n) {
                LOG(Info, "AAP listening mode: {}", Helper::ToString(n.mode));
            },
            [](const Aap::ConversationalAwarenessNotification &ca) {
                LOG(Info, "AAP conversation awareness: {}", ca.enabled);
            },
            [](const Aap::ConversationalAwarenessSpeechNotification &speech) {
                LOG(Info, "AAP conversation speech: level={}, speaking={}", speech.level,
                    speech.speaking);
            },
            [](const Aap::HeartRateNotification &hr) {
                const int confPct = (static_cast<int>(hr.confidence) * 100 + 127) / 255;
                LOG(Info, "AAP optical heart rate: {} BPM (confidence {}%)", hr.bpm, confPct);
            },
            [](const Aap::MetadataNotification &m) {
                LOG(Info, "AAP metadata: name='{}' model='{}' fw='{}'", m.deviceName.toStdString(),
                    m.modelNumber.toStdString(), m.firmwareVersion.toStdString());
            },
            [](const auto &) {},
        },
        msg);
}

bool IsRtBuddyFrame(std::span<const uint8_t> packet)
{
    return packet.size() >= 6 && packet[0] == 0x04 && packet[1] == 0x00 && packet[2] == 0x04 &&
           packet[3] == 0x00 && packet[4] == 0x17 && packet[5] == 0x00;
}

bool IsHandshakeAckFrame(std::span<const uint8_t> packet)
{
    return packet.size() >= 6 && packet[0] == 0x01 && packet[1] == 0x00 && packet[2] == 0x04 &&
           packet[3] == 0x00 && (packet[4] == 0x00 || packet[4] == 0x85);
}

} // namespace

Manager::Manager(QObject *parent, std::unique_ptr<ControlTransport> controlTransport)
    : QObject{parent},
      _controlTransport{
          controlTransport ? std::move(controlTransport) : std::make_unique<NullControlTransport>()}
{
    _expiryTimer.setSingleShot(true);
    connect(&_expiryTimer, &QTimer::timeout, this, &Manager::OnExpiryTimeout);

    _heartRateWatchdog.setSingleShot(true);
    connect(&_heartRateWatchdog, &QTimer::timeout, this, &Manager::OnHeartRateWatchdogTimeout);

    _convSpeechSafetyTimer.setSingleShot(true);
    connect(
        &_convSpeechSafetyTimer, &QTimer::timeout, this,
        &Manager::OnConversationSpeechSafetyTimeout);

    _stateMgr.SetOnDiscardState([this] {
        QMetaObject::invokeMethod(this, [this] { emit Disconnected(); }, Qt::QueuedConnection);
    });

    _controlTransport->SetOnConnectionChanged(
        [this](bool connected) { OnControlConnectionChanged(connected); });
    _controlTransport->SetOnPacketReceived(
        [this](std::vector<uint8_t> packet) { OnControlPacketReceived(packet); });

    // Callbacks acquire `_mutex` internally and ALWAYS release it before emitting signals.
    _adWatcher.CbReceived() +=
        [this](auto &&...args) { OnAdvertisementReceived(std::forward<decltype(args)>(args)...); };

    _adWatcher.CbStateChanged() +=
        [this](auto &&...args) { OnAdvWatcherStateChanged(std::forward<decltype(args)>(args)...); };
}

Manager::~Manager()
{
    _expiryTimer.stop();
    _heartRateWatchdog.stop();
    _convSpeechSafetyTimer.stop();
    if (_heartRateWanted && _aapConnected && _controlTransport->IsConnected()) {
        StopHeartRateWire(false);
    }
    _controlTransport->SetOnPacketReceived({});
    _controlTransport->SetOnConnectionChanged({});
    _controlTransport->Disconnect();

    StopScanner();

    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }
}

bool Manager::ShouldScan() const
{
    if (_systemSuspended) {
        return false;
    }
    // When an AAP control link is live, battery, ear detection and listening mode arrive
    // as push notifications over ACL/L2CAP, so BLE scanning can rest at 0% CPU.
    if (_aapConnected) {
        return false;
    }
    if (_deviceConnected) {
        return _boundDevice.has_value();
    }
    return _scanWhileDisconnected || _showNearbyWhenDisconnected || _showPopupOnLidOpen;
}

void Manager::ReconcileScannerLater()
{
    // Start and stop take the watcher's own locks and wait for its callbacks, so they never
    // run from inside one; the decision is re-read on the manager's thread.
    QMetaObject::invokeMethod(
        this,
        [this] {
            bool wanted;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                wanted = ShouldScan();
            }
            if (wanted) {
                StartScanner();
            }
            else {
                StopScanner();
            }
        },
        Qt::QueuedConnection);
}

void Manager::ScheduleExpiryTimerLater()
{
    QMetaObject::invokeMethod(
        this,
        [this] {
            std::optional<std::chrono::milliseconds> nextMs;
            if (const auto stateExpiry = _stateMgr.NextExpiry(); stateExpiry.has_value()) {
                nextMs = std::chrono::duration_cast<std::chrono::milliseconds>(stateExpiry.value());
            }

            {
                std::lock_guard<std::mutex> lock{_mutex};
                if (_lastNearbyState.has_value() && !_deviceConnected) {
                    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - _lastNearbySeen);
                    const auto remaining = std::max(
                        std::chrono::milliseconds{50}, std::chrono::milliseconds{12000} - elapsed);
                    if (!nextMs.has_value() || remaining < nextMs.value()) {
                        nextMs = remaining;
                    }
                }
            }

            if (!nextMs.has_value()) {
                _expiryTimer.stop();
                return;
            }
            const auto ms = std::max<int>(50, static_cast<int>(nextMs->count()));
            _expiryTimer.start(ms);
        },
        Qt::QueuedConnection);
}

void Manager::OnExpiryTimeout()
{
    _stateMgr.ExpireStale();
    bool clearNearby = false;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_lastNearbyState.has_value() && !_deviceConnected &&
            std::chrono::steady_clock::now() - _lastNearbySeen >= std::chrono::seconds{12})
        {
            _lastNearbyState.reset();
            _lastNearbyLidOpen = false;
            clearNearby = true;
        }
    }
    if (clearNearby) {
        emit NearbyCleared();
    }
    ScheduleExpiryTimerLater();
}

void Manager::StartScanner()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!ShouldScan()) {
            LOG(Info, "Skip Bluetooth AdvWatcher start: no bound device, suspended, AAP active, "
                      "or disconnected with auto-connect off.");
            return;
        }
        if (_scannerWanted) {
            return;
        }
        _scannerWanted = true;
    }

    if (!_adWatcher.Start()) {
        std::lock_guard<std::mutex> lock{_mutex};
        _scannerWanted = false;
        LOG(Warn, "Bluetooth AdvWatcher start failed.");
    }
    else {
        LOG(Info, "Bluetooth AdvWatcher start succeeded.");
    }
}

void Manager::StopScanner()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_scannerWanted) {
            return;
        }
        _scannerWanted = false;
    }

    if (!_adWatcher.Stop()) {
        LOG(Warn, "AsyncScanner::Stop() failed.");
    }
    else {
        LOG(Info, "AsyncScanner::Stop() succeeded.");
    }
}

void Manager::OnRssiMinChanged(int16_t rssiMin)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _rssiMin = rssiMin;
    _stateMgr.OnRssiMinChanged(rssiMin);
}

void Manager::SetScanWhileDisconnected(bool enable)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_scanWhileDisconnected == enable) {
            return;
        }
        _scanWhileDisconnected = enable;
    }

    LOG(Info, "Scan while disconnected: {}", enable);
    ReconcileScannerLater();
}

void Manager::SetShowNearbyWhenDisconnected(bool enable)
{
    bool clearNearby = false;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_showNearbyWhenDisconnected == enable) {
            return;
        }
        _showNearbyWhenDisconnected = enable;
        if (!enable && _lastNearbyState.has_value()) {
            _lastNearbyState.reset();
            _lastNearbyLidOpen = false;
            clearNearby = true;
        }
    }
    if (clearNearby) {
        emit NearbyCleared();
    }
    LOG(Info, "Show nearby popup while disconnected: {}", enable);
    ReconcileScannerLater();
}

void Manager::SetShowPopupOnLidOpen(bool enable)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_showPopupOnLidOpen == enable) {
            return;
        }
        _showPopupOnLidOpen = enable;
    }
    LOG(Info, "Show popup on lid open: {}", enable);
    ReconcileScannerLater();
}

bool Manager::SendAapControl(
    std::vector<uint8_t> packet, std::optional<Aap::InboundMessage> optimisticMsg,
    std::vector<uint8_t> followUpPacket)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_aapConnected || !_deviceConnected || !_controlTransport->IsConnected()) {
            return false;
        }
    }

    if (!_controlTransport->SendPacket(packet)) {
        return false;
    }
    if (!followUpPacket.empty()) {
        _controlTransport->SendPacket(followUpPacket);
    }

    PendingStateSignals pending;
    if (optimisticMsg.has_value()) {
        std::lock_guard<std::mutex> lock{_mutex};
        if (auto update = _stateMgr.OnAapMessage(optimisticMsg.value(), _boundModel);
            update.has_value())
        {
            pending = PrepareStateChangedLocked(std::move(update.value()));
        }
    }
    EmitStateSignals(pending);
    return true;
}

bool Manager::SetNoiseControlMode(NoiseControlMode mode)
{
    bool connected = false;
    bool alreadyInMode = false;
    bool queuedBehindInFlight = false;
    bool supportsAllowOff = false;
    bool needAllowOffFirst = false;
    bool needFeaturesFirst = false;
    uint64_t inFlightEpoch = 0;
    PendingStateSignals pending;

    {
        std::lock_guard<std::mutex> lock{_mutex};
        connected = _aapConnected && _deviceConnected && _controlTransport->IsConnected();
        if (!connected) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto state = _stateMgr.GetCurrentState();
        const bool hasPendingSpecial =
            (_pendingOffModeUntil != std::chrono::steady_clock::time_point{} &&
             now < _pendingOffModeUntil) ||
            (_pendingAdaptiveUntil != std::chrono::steady_clock::time_point{} &&
             now < _pendingAdaptiveUntil);

        if (!_noiseModeInFlight && !hasPendingSpecial && _lastRequestedNoiseMode == mode &&
            state.has_value() && state->noiseControl.activeMode == mode)
        {
            alreadyInMode = true;
        }
        else if (_noiseModeInFlight) {
            // AirPods Pro 2/3 silently drop a second 0x0D packet sent while an inter-bud mode
            // transition is still underway. Update optimistic UI immediately and hold the latest
            // target in `_queuedNoiseMode` so it dispatches the moment the current echo lands.
            queuedBehindInFlight = true;
            _lastRequestedNoiseMode = mode;
            if (mode == _inFlightNoiseMode) {
                _queuedNoiseMode.reset();
            }
            else {
                _queuedNoiseMode = mode;
            }
            if (mode == NoiseControlMode::Adaptive) {
                _adaptiveRequestedAt = now;
            }
            else {
                _pendingAdaptiveLevelToSend.reset();
            }
            if (auto u = _stateMgr.OnAapMessage(
                    Aap::NoiseControlNotification{.mode = mode}, _boundModel);
                u.has_value())
            {
                pending = PrepareStateChangedLocked(std::move(u.value()));
            }
        }
        else {
            _lastRequestedNoiseMode = mode;
            _noiseModeInFlight = true;
            _inFlightNoiseMode = mode;
            _queuedNoiseMode.reset();
            inFlightEpoch = ++_noiseModeInFlightEpoch;

            const Model model =
                state.has_value() && state->model != Model::Unknown ? state->model : _boundModel;
            supportsAllowOff = CapabilitiesFor(model).supportsAllowOffMode;

            if (mode == NoiseControlMode::Off && supportsAllowOff) {
                ++_pendingOffModeEpoch;
                _offModeRetrySent = false;
                ++_pendingAdaptiveEpoch;
                _pendingAdaptiveUntil = {};
                _adaptiveRequestedAt = {};
                _adaptiveRetrySent = false;
                _pendingAdaptiveLevelToSend.reset();
                _userDisabledAllowOff = false;
                needAllowOffFirst =
                    !_allowOffSentToDevice || !state.has_value() ||
                    state->noiseControl.allowOffListeningMode != std::optional{true};
                _allowOffSentToDevice = true;
                const int sendOffsetMs = needAllowOffFirst ? 160 : 0;
                _pendingOffModeSendAt = now + std::chrono::milliseconds{sendOffsetMs};
                _pendingOffModeUntil = now + std::chrono::milliseconds{sendOffsetMs + 950};

                if (needAllowOffFirst) {
                    if (auto u1 = _stateMgr.OnAapMessage(
                            Aap::FeatureToggleNotification{
                                .feature = Aap::ToggleFeature::AllowOffListeningMode,
                                .enabled = true},
                            _boundModel);
                        u1.has_value())
                    {
                        pending = PrepareStateChangedLocked(std::move(u1.value()));
                    }
                }
                if (auto u2 = _stateMgr.OnAapMessage(
                        Aap::NoiseControlNotification{.mode = NoiseControlMode::Off}, _boundModel);
                    u2.has_value())
                {
                    pending = PrepareStateChangedLocked(std::move(u2.value()));
                }
            }
            else if (mode == NoiseControlMode::Adaptive) {
                ++_pendingOffModeEpoch;
                _pendingOffModeUntil = {};
                _pendingOffModeSendAt = {};
                _offModeRetrySent = false;
                ++_pendingAdaptiveEpoch;
                _adaptiveRetrySent = false;
                _adaptiveRequestedAt = now;
                needFeaturesFirst = !_aapFeaturesEnabledSent;
                _aapFeaturesEnabledSent = true;
                const int sendOffsetMs = needFeaturesFirst ? 140 : 0;
                _pendingAdaptiveUntil = now + std::chrono::milliseconds{sendOffsetMs + 950};
                if (auto u = _stateMgr.OnAapMessage(
                        Aap::NoiseControlNotification{.mode = NoiseControlMode::Adaptive},
                        _boundModel);
                    u.has_value())
                {
                    pending = PrepareStateChangedLocked(std::move(u.value()));
                }
            }
            else {
                ++_pendingOffModeEpoch;
                _pendingOffModeUntil = {};
                _pendingOffModeSendAt = {};
                _offModeRetrySent = false;
                ++_pendingAdaptiveEpoch;
                _pendingAdaptiveUntil = {};
                _adaptiveRequestedAt = {};
                _adaptiveRetrySent = false;
                _pendingAdaptiveLevelToSend.reset();
                if (auto u = _stateMgr.OnAapMessage(
                        Aap::NoiseControlNotification{.mode = mode}, _boundModel);
                    u.has_value())
                {
                    pending = PrepareStateChangedLocked(std::move(u.value()));
                }
            }
        }
    }

    if (alreadyInMode) {
        return true;
    }

    if (queuedBehindInFlight) {
        LOG(Info, "AAP: queued listening mode '{}' behind in-flight hardware transition.",
            Helper::ToString(mode));
        EmitStateSignals(pending);
        return true;
    }

    LOG(Info, "AAP: requesting noise control mode '{}'.", Helper::ToString(mode));
    EmitStateSignals(pending);
    DispatchNoiseControlWire(
        mode, supportsAllowOff, needAllowOffFirst, needFeaturesFirst, inFlightEpoch);
    return true;
}

void Manager::DispatchNoiseControlWire(
    NoiseControlMode mode, bool supportsAllowOff, bool needAllowOffFirst, bool needFeaturesFirst,
    uint64_t inFlightEpoch)
{
    const auto sendAfter = [this, inFlightEpoch](int delayMs, NoiseControlMode targetMode) {
        if (delayMs <= 0) {
            _controlTransport->SendPacket(Aap::BuildSetNoiseControlPacket(targetMode));
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, inFlightEpoch, delayMs, targetMode] {
                QTimer::singleShot(delayMs, this, [this, inFlightEpoch, targetMode] {
                    {
                        std::lock_guard<std::mutex> lock{_mutex};
                        if (!_aapConnected || _noiseModeInFlightEpoch != inFlightEpoch ||
                            !_controlTransport->IsConnected())
                        {
                            return;
                        }
                    }
                    _controlTransport->SendPacket(Aap::BuildSetNoiseControlPacket(targetMode));
                });
            },
            Qt::QueuedConnection);
    };

    if (mode == NoiseControlMode::Off && supportsAllowOff && needAllowOffFirst) {
        LOG(Info,
            "AAP: enabling Allow \"Off\" Listening Mode (0x34) and scheduling Listening Off in "
            "160 ms.");
        _controlTransport->SendPacket(Aap::BuildSetAllowOffListeningModePacket(true));
        sendAfter(160, NoiseControlMode::Off);
    }
    else if (mode == NoiseControlMode::Adaptive && needFeaturesFirst) {
        LOG(Info, "AAP: enabling H2 features (0x4D) before switching to Adaptive.");
        _controlTransport->SendPacket(Aap::BuildEnableFeaturesPacket());
        sendAfter(140, NoiseControlMode::Adaptive);
    }
    else {
        sendAfter(0, mode);
    }

    // Safety guard: release the in-flight slot if AirPods never echo (e.g. hardware was already
    // in that mode or a transient RF glitch dropped the echo) so queued clicks never stall.
    // AirPods Pro 3 back-to-back Adaptive/Off transitions can take up to ~2.7 s, so allow 3.2 s
    // before forcing a timeout flush.
    QMetaObject::invokeMethod(
        this,
        [this, inFlightEpoch] {
            QTimer::singleShot(3200, this, [this, inFlightEpoch] {
                FlushQueuedNoiseModeAfterTransition(inFlightEpoch);
            });
        },
        Qt::QueuedConnection);
}

void Manager::FlushQueuedNoiseModeAfterTransition(uint64_t expectedEpoch)
{
    std::optional<NoiseControlMode> nextMode;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_noiseModeInFlight || _noiseModeInFlightEpoch != expectedEpoch) {
            return;
        }
        _noiseModeInFlight = false;
        nextMode = _queuedNoiseMode;
        _queuedNoiseMode.reset();
        if (nextMode.has_value()) {
            _lastRequestedNoiseMode.reset();
        }
    }
    if (nextMode.has_value()) {
        SetNoiseControlMode(nextMode.value());
    }
}

bool Manager::CycleNoiseControlMode()
{
    NoiseControlMode next = NoiseControlMode::Off;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_aapConnected || !_deviceConnected || !_controlTransport->IsConnected()) {
            return false;
        }

        const auto state = _stateMgr.GetCurrentState();
        const Model model =
            state.has_value() && state->model != Model::Unknown ? state->model : _boundModel;
        const auto caps = CapabilitiesFor(model);
        if (!caps.SupportsNoiseControl()) {
            return false;
        }

        std::vector<NoiseControlMode> modes{NoiseControlMode::Off};
        if (caps.supportsNoiseCancellation) {
            modes.push_back(NoiseControlMode::NoiseCancellation);
        }
        if (caps.supportsTransparency) {
            modes.push_back(NoiseControlMode::Transparency);
        }
        if (caps.supportsAdaptiveAudio) {
            modes.push_back(NoiseControlMode::Adaptive);
        }

        const NoiseControlMode current =
            (state.has_value() && state->noiseControl.activeMode.has_value())
                ? state->noiseControl.activeMode.value()
                : NoiseControlMode::Off;

        auto it = std::find(modes.begin(), modes.end(), current);
        const size_t nextIndex =
            it == modes.end()
                ? 0
                : (static_cast<size_t>(std::distance(modes.begin(), it)) + 1) % modes.size();
        next = modes[nextIndex];
    }

    LOG(Info, "AAP: cycling noise control mode to '{}'.", Helper::ToString(next));
    return SetNoiseControlMode(next);
}

bool Manager::SetConversationalAwareness(bool enabled)
{
    LOG(Info, "AAP: requesting conversational awareness '{}'.", enabled);
    if (!enabled) {
        QMetaObject::invokeMethod(
            this, [this] { _convSpeechSafetyTimer.stop(); }, Qt::QueuedConnection);
        emit ConversationAwarenessLevelChanged(0);
    }
    return SendAapControl(
        Aap::BuildSetConversationalAwarenessPacket(enabled),
        Aap::ConversationalAwarenessNotification{.enabled = enabled});
}

bool Manager::SetOneBudAnc(bool enabled)
{
    return SendAapControl(
        Aap::BuildSetOneBudAncPacket(enabled),
        Aap::FeatureToggleNotification{
            .feature = Aap::ToggleFeature::OneBudAnc, .enabled = enabled});
}

bool Manager::SetPersonalizedVolume(bool enabled)
{
    return SendAapControl(
        Aap::BuildSetPersonalizedVolumePacket(enabled),
        Aap::FeatureToggleNotification{
            .feature = Aap::ToggleFeature::PersonalizedVolume, .enabled = enabled});
}

bool Manager::SetLoudSoundReduction(bool enabled)
{
    return SendAapControl(
        Aap::BuildSetLoudSoundReductionPacket(enabled),
        Aap::FeatureToggleNotification{
            .feature = Aap::ToggleFeature::LoudSoundReduction, .enabled = enabled});
}

bool Manager::SetAdaptiveNoiseLevel(uint8_t level)
{
    const uint8_t clamped = std::min<uint8_t>(level, 100);
    bool deferForModeSwitch = false;
    uint64_t levelEpoch = 0;
    PendingStateSignals pending;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_aapConnected || !_deviceConnected || !_controlTransport->IsConnected()) {
            return false;
        }
        levelEpoch = ++_adaptiveLevelEpoch;
        const auto now = std::chrono::steady_clock::now();
        deferForModeSwitch = _adaptiveRequestedAt != std::chrono::steady_clock::time_point{} &&
                             now - _adaptiveRequestedAt < std::chrono::milliseconds{400};
        if (deferForModeSwitch) {
            _pendingAdaptiveLevelToSend = clamped;
        }
        else {
            _pendingAdaptiveLevelToSend.reset();
        }
        if (auto u = _stateMgr.OnAapMessage(
                Aap::AdaptiveNoiseLevelNotification{.level = clamped}, _boundModel);
            u.has_value())
        {
            pending = PrepareStateChangedLocked(std::move(u.value()));
        }
    }

    EmitStateSignals(pending);

    if (deferForModeSwitch) {
        QMetaObject::invokeMethod(
            this,
            [this, levelEpoch, clamped] {
                QTimer::singleShot(380, this, [this, levelEpoch, clamped] {
                    {
                        std::lock_guard<std::mutex> lock{_mutex};
                        if (!_aapConnected || _adaptiveLevelEpoch != levelEpoch ||
                            !_pendingAdaptiveLevelToSend.has_value() ||
                            !_controlTransport->IsConnected())
                        {
                            return;
                        }
                        _pendingAdaptiveLevelToSend.reset();
                    }
                    _controlTransport->SendPacket(Aap::BuildSetAdaptiveNoiseLevelPacket(clamped));
                });
            },
            Qt::QueuedConnection);
        return true;
    }

    return _controlTransport->SendPacket(Aap::BuildSetAdaptiveNoiseLevelPacket(clamped));
}

bool Manager::SetHeadGestures(bool enabled)
{
    return SendAapControl(
        Aap::BuildSetHeadGesturesPacket(enabled),
        Aap::FeatureToggleNotification{
            .feature = Aap::ToggleFeature::HeadGestures, .enabled = enabled});
}

bool Manager::SetMicrophoneSide(MicrophoneSide side)
{
    return SendAapControl(
        Aap::BuildSetMicrophoneSidePacket(side), Aap::MicrophoneSideNotification{.side = side});
}

bool Manager::SetToneVolume(uint8_t volumePercent)
{
    return SendAapControl(
        Aap::BuildSetToneVolumePacket(volumePercent),
        Aap::ToneVolumeNotification{.volume = volumePercent});
}

bool Manager::SetAllowOffListeningMode(bool allow)
{
    bool switchToTransparency = false;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _userDisabledAllowOff = !allow;
        _allowOffSentToDevice = allow;
        if (!allow) {
            ++_pendingOffModeEpoch;
            _pendingOffModeUntil = {};
            const auto st = _stateMgr.GetCurrentState();
            switchToTransparency =
                st.has_value() && st->noiseControl.activeMode == NoiseControlMode::Off;
        }
    }
    const bool ok = SendAapControl(
        Aap::BuildSetAllowOffListeningModePacket(allow),
        Aap::FeatureToggleNotification{
            .feature = Aap::ToggleFeature::AllowOffListeningMode, .enabled = allow});
    if (ok && switchToTransparency) {
        SetNoiseControlMode(NoiseControlMode::Transparency);
    }
    return ok;
}

bool Manager::SetListeningModeCycle(uint8_t modeMask)
{
    return SendAapControl(
        Aap::BuildSetListeningModeCyclePacket(modeMask),
        Aap::ListeningModeCycleNotification{.modeMask = modeMask});
}

bool Manager::SetPressSpeed(PressSpeed speed)
{
    return SendAapControl(
        Aap::BuildSetPressSpeedPacket(speed), Aap::PressSpeedNotification{.speed = speed});
}

bool Manager::SetPressHoldDuration(PressHoldDuration duration)
{
    return SendAapControl(
        Aap::BuildSetPressHoldDurationPacket(duration),
        Aap::PressHoldDurationNotification{.duration = duration});
}

bool Manager::SetHearingAid(bool enabled)
{
    return SendAapControl(
        Aap::BuildSetHearingAidPacket(enabled),
        Aap::FeatureToggleNotification{
            .feature = Aap::ToggleFeature::HearingAid, .enabled = enabled});
}

bool Manager::SetMediaAssist(bool enabled)
{
    return SendAapControl(
        Aap::BuildSetMediaAssistPacket(enabled),
        Aap::FeatureToggleNotification{
            .feature = Aap::ToggleFeature::MediaAssist, .enabled = enabled});
}

bool Manager::SetHeartRateMonitoring(bool enabled)
{
    LOG(Info, "AAP: requesting RTBuddy optical heart rate sensor '{}'.", enabled);

    bool connected = false;
    bool notWorn = false;
    PendingStateSignals pending;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        connected = _aapConnected && _deviceConnected && _controlTransport->IsConnected();
        if (!connected) {
            return false;
        }
        _heartRateWanted = enabled;
        _heartRateUserDisabled = !enabled;
        _heartRateRetrying = false;
        _heartRateWarmupLogged = false;
        _lastLoggedBpm.reset();
        ++_heartRateEpoch;

        const auto st = _stateMgr.GetCurrentState();
        const bool worn = st.has_value() && (st->pods.left.isInEar || st->pods.right.isInEar);
        const bool bothInCase = st.has_value() && st->caseBox.isBothPodsInCase && !worn;
        notWorn = bothInCase || (_aapEarStateReceived && !worn);
        _heartRatePausedOutOfEar = enabled && notWorn;

        if (auto u1 = _stateMgr.OnAapMessage(
                Aap::FeatureToggleNotification{
                    .feature = Aap::ToggleFeature::HeartRateMonitoring, .enabled = enabled},
                _boundModel);
            u1.has_value())
        {
            pending = PrepareStateChangedLocked(std::move(u1.value()));
        }
        const HeartRateStatus nextStatus =
            !enabled ? HeartRateStatus::Off
                     : (notWorn ? HeartRateStatus::WaitingForEar : HeartRateStatus::Starting);
        if (auto u2 = _stateMgr.OnAapMessage(
                Aap::HeartRateStatusNotification{.status = nextStatus}, _boundModel);
            u2.has_value())
        {
            pending = PrepareStateChangedLocked(std::move(u2.value()));
        }
    }

    EmitStateSignals(pending);

    if (enabled) {
        if (!notWorn) {
            StartHeartRateCampaignLater(true);
        }
        else {
            LOG(Info, "AAP heart rate: AirPods are not in ears; waiting for ear contact to start.");
        }
    }
    else {
        QMetaObject::invokeMethod(
            this, [this] { _heartRateWatchdog.stop(); }, Qt::QueuedConnection);
        StopHeartRateWire(true);
    }
    return true;
}

bool Manager::RenameDevice(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        return false;
    }
    return SendAapControl(Aap::BuildSetRenamePacket(name.trimmed().toStdString()));
}

void Manager::SetAudioProfile(AudioProfile profile, QString sourceProcess)
{
    PendingStateSignals pending;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (auto update = _stateMgr.SetAudioProfile(profile, std::move(sourceProcess));
            update.has_value())
        {
            pending = PrepareStateChangedLocked(std::move(update.value()));
        }
    }
    EmitStateSignals(pending);
}

void Manager::SetLowLatencyMode(bool enabled)
{
    if (_controlTransport) {
        _controlTransport->SetLowLatencyMode(enabled);
    }
}

std::optional<RadioServiceInfo> Manager::GetRadioServiceInfo() const
{
    return _controlTransport ? _controlTransport->GetRadioServiceInfo() : std::nullopt;
}

bool Manager::ApplyRadioProxyOptions(const RadioProxyOptions &options)
{
    if (!_controlTransport) {
        return false;
    }
    const bool ok = _controlTransport->ApplyRadioProxyOptions(options);
    uint64_t addr = 0;
    QString name;
    bool supportsStereoMic = false;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_boundDevice.has_value()) {
            addr = _boundDevice->GetAddress();
            name = Details::ResolveDisplayName(_deviceName, _boundModel);
            supportsStereoMic = CapabilitiesFor(_boundModel).supportsStereoA2dpMic;
        }
    }
    if (addr != 0) {
        _controlTransport->ConfigureVirtualMic(
            addr, options.virtualMicEnabled, !supportsStereoMic, name.toStdString());
    }
    return ok;
}

bool Manager::RetryRadioServiceAfterGuard()
{
    return _controlTransport && _controlTransport->RetryRadioServiceAfterGuard();
}

bool Manager::RestoreNativeRadioDriver()
{
    return _controlTransport && _controlTransport->RestoreNativeRadioDriver();
}

void Manager::ReconnectControlTransport()
{
    uint64_t addr = 0;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_deviceConnected && _boundDevice.has_value()) {
            addr = _boundDevice->GetAddress();
        }
    }
    if (addr != 0 && _controlTransport && _controlTransport->IsAvailable()) {
        _controlTransport->Connect(addr);
    }
}

void Manager::DisconnectControlTransport()
{
    if (_controlTransport) {
        _controlTransport->Disconnect();
    }
    PendingStateSignals pending;
    bool emitControlDropped = false;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_aapConnected) {
            _aapConnected = false;
            emitControlDropped = true;
            if (auto update = _stateMgr.SetAapConnected(false, _boundModel); update.has_value()) {
                pending = PrepareStateChangedLocked(std::move(update.value()));
            }
        }
    }
    EmitStateSignals(pending);
    if (emitControlDropped) {
        emit ControlConnectionChanged(false);
    }
    ScheduleExpiryTimerLater();
    ReconcileScannerLater();
}

void Manager::RebindAfterRadioChange()
{
    uint64_t addr = 0;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        addr = _requestedDeviceAddress;
    }
    if (addr != 0) {
        LOG(Info, "Rebinding Bluetooth device handle after radio driver change.");
        OnBoundDeviceAddressChanged(addr);
    }
}

Manager::DiagnosticsSnapshot Manager::GetDiagnostics() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    DiagnosticsSnapshot snap;
    snap.boundDeviceName = Details::ResolveDisplayName(_deviceName, _boundModel);
    snap.boundModel = _boundModel;
    snap.bluetoothConnected = _deviceConnected;
    snap.aapDriverAvailable = _controlTransport->IsAvailable();
    snap.aapConnected = _aapConnected && _deviceConnected && _controlTransport->IsConnected();
    snap.transportBackend = QString::fromStdString(_controlTransport->BackendName());
    snap.radioServiceInfo = _controlTransport->GetRadioServiceInfo();
    snap.scannerRunning = _scannerWanted;
    snap.lastRssi = _lastRssi;
    snap.lastAdvHex = _lastAdvHex;
    snap.heartRateServiceId = _rtBuddyParser.HeartRateService();
    snap.currentState = _stateMgr.GetCurrentState();
    if (snap.currentState.has_value() && snap.currentState->displayName.isEmpty()) {
        snap.currentState->displayName =
            Details::ResolveDisplayName(_deviceName, snap.currentState->model);
    }
    return snap;
}

void Manager::OnSystemSuspend()
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _systemSuspended = true;
    }
    LOG(Info, "System suspending: pausing BLE scanner.");
    _controlTransport->Disconnect();
    StopScanner();
}

void Manager::OnSystemResume()
{
    std::optional<Bluetooth::DeviceState> currentState;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _systemSuspended = false;
        if (_boundDevice.has_value()) {
            currentState = _boundDevice->GetConnectionState();
        }
    }
    LOG(Info, "System resumed: reconciling Bluetooth state.");
    if (currentState.has_value()) {
        HandleBoundDeviceConnectionStateChanged(currentState.value());
    }
    ReconcileScannerLater();
}

void Manager::OnBoundDeviceAddressChanged(uint64_t address)
{
    _deviceLookupThread.request_stop();
    if (_deviceLookupThread.joinable()) {
        _deviceLookupThread.join();
    }

    _controlTransport->Disconnect();

    bool wasAapConnected = false;
    {
        std::lock_guard<std::mutex> lock{_mutex};

        _requestedDeviceAddress = address;
        _boundDevice.reset();
        _deviceName.clear();
        _boundContainerId.clear();
        _boundModel = Model::Unknown;
        _deviceConnected = false;
        wasAapConnected = _aapConnected;
        _aapConnected = false;
        _lastRssi.reset();
        _lastAdvHex.clear();
        _seenUnknownAapOpcodes.clear();
        _stateMgr.Disconnect();
    }

    emit DeviceConnectionChanged(false);
    if (wasAapConnected) {
        emit ControlConnectionChanged(false);
    }

    ScheduleExpiryTimerLater();
    ReconcileScannerLater();

    if (address == 0) {
        LOG(Info, "Unbind device.");
        return;
    }

    LOG(Info, "Bind a new device.");

    _deviceLookupThread = std::jthread{[this, address](std::stop_token stopToken) {
        Support::OS::Windows::Winrt::Initialize();
        auto device = Bluetooth::DeviceManager::FindDevice(address);
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, address, device = std::move(device)]() mutable {
                CompleteBoundDeviceLookup(address, std::move(device));
            },
            Qt::QueuedConnection);
    }};
}

void Manager::CompleteBoundDeviceLookup(uint64_t address, std::optional<Bluetooth::Device> device)
{
    QString resolvedDisplayName;
    Model resolvedModel = Model::Unknown;
    QString resolvedContainerId;
    bool unavailable = false;
    Bluetooth::DeviceState initialState = Bluetooth::DeviceState::Disconnected;

    {
        std::lock_guard<std::mutex> lock{_mutex};

        if (address != _requestedDeviceAddress) {
            LOG(Info, "Ignore a stale bound-device lookup result.");
            return;
        }

        if (!device.has_value()) {
            LOG(Warn, "The bound device address is no longer available.");
            unavailable = true;
        }
        else {
            _boundDevice = std::move(device);
            _boundModel = AppleCP::AirPods::GetModel(_boundDevice->GetProductId());

            _deviceName = QString::fromStdString([&] {
                auto name = _boundDevice->GetName();
                return name.find("Bluetooth") != std::string::npos ? std::string{} : name;
            }());
            _boundContainerId = QString::fromStdString(_boundDevice->GetContainerId());

            resolvedDisplayName = Details::ResolveDisplayName(_deviceName, _boundModel);
            resolvedModel = _boundModel;
            resolvedContainerId = _boundContainerId;

            _boundDevice->CbConnectionStatusChanged() += [this](auto &&...args) {
                HandleBoundDeviceConnectionStateChanged(std::forward<decltype(args)>(args)...);
            };

            initialState = _boundDevice->GetConnectionState();
        }
    }

    if (unavailable) {
        emit BoundDeviceUnavailable();
        return;
    }

    emit BoundDeviceResolved(resolvedDisplayName, resolvedModel, resolvedContainerId);
    HandleBoundDeviceConnectionStateChanged(initialState);
}

void Manager::HandleBoundDeviceConnectionStateChanged(Bluetooth::DeviceState state)
{
    bool oldDeviceConnected = false;
    bool newDeviceConnected = (state == Bluetooth::DeviceState::Connected);
    bool doDisconnect = false;
    bool wasAapConnected = false;
    uint64_t connectAddr = 0;

    {
        std::lock_guard<std::mutex> lock{_mutex};
        oldDeviceConnected = _deviceConnected;
        doDisconnect = oldDeviceConnected && !newDeviceConnected;
        _deviceConnected = newDeviceConnected;
        if (newDeviceConnected) {
            _lastNearbyState.reset();
            _lastNearbyLidOpen = false;
            if (_boundDevice.has_value()) {
                connectAddr = _boundDevice->GetAddress();
            }
        }
        else if (doDisconnect) {
            wasAapConnected = _aapConnected;
            _aapConnected = false;
            _stateMgr.Disconnect();
        }
    }

    LOG(Info, "The device we bound is updated. current: {}, new: {}", oldDeviceConnected,
        newDeviceConnected);

    emit DeviceConnectionChanged(newDeviceConnected);

    if (newDeviceConnected && connectAddr != 0 && _controlTransport->IsAvailable()) {
        _controlTransport->Connect(connectAddr);
    }
    else if (doDisconnect) {
        _controlTransport->Disconnect();
        if (wasAapConnected) {
            emit ControlConnectionChanged(false);
        }
        ScheduleExpiryTimerLater();
    }

    ReconcileScannerLater();
}

void Manager::OnControlConnectionChanged(bool connected)
{
    bool shouldHandshake = false;
    bool rejectStaleConnect = false;
    bool resumeHeartRate = false;
    uint64_t connEpoch = 0;
    PendingStateSignals pending;

    {
        std::lock_guard<std::mutex> lock{_mutex};
        _rtBuddyReassembler.Reset();
        _rtBuddyParser.Reset();
        _rtBuddySeq = 0;
        _heartRateWarmupLogged = false;
        _heartRateRetrying = false;
        _aapEarStateReceived = false;
        _lastLoggedBpm.reset();
        _aapFeaturesEnabledSent = false;
        _allowOffSentToDevice = false;
        _pendingOffModeUntil = {};
        _pendingOffModeSendAt = {};
        _offModeRetrySent = false;
        ++_pendingOffModeEpoch;
        _pendingAdaptiveUntil = {};
        _adaptiveRequestedAt = {};
        _adaptiveRetrySent = false;
        ++_pendingAdaptiveEpoch;
        ++_adaptiveLevelEpoch;
        _pendingAdaptiveLevelToSend.reset();
        _noiseModeInFlight = false;
        _queuedNoiseMode.reset();
        ++_noiseModeInFlightEpoch;
        _lastRequestedNoiseMode.reset();
        connEpoch = ++_aapConnectionEpoch;
        ++_heartRateEpoch;

        if (connected && !_deviceConnected) {
            rejectStaleConnect = true;
            _aapConnected = false;
        }
        else {
            _aapConnected = connected;
            shouldHandshake = connected;
            resumeHeartRate = connected && _heartRateWanted;
            if (auto update = _stateMgr.SetAapConnected(connected, _boundModel); update.has_value())
            {
                pending = PrepareStateChangedLocked(std::move(update.value()));
            }
        }
    }

    if (rejectStaleConnect) {
        LOG(Info,
            "Ignoring stale AAP connection notification while bound AirPods are disconnected.");
        _controlTransport->Disconnect();
        return;
    }

    if (shouldHandshake) {
        LOG(Info, "AAP control transport connected; starting handshake.");
        // Send ONLY the AAP handshake first. Sending 0x4D (EnableFeatures) and 0x0F
        // (RequestNotifications) before AirPods reply with the Handshake ACK (01 00 04 00 00 00)
        // causes AirPods Pro 2/3 to drop both frames — delaying metadata/modes by 30 s and
        // causing Adaptive (0x04) requests to fall back to Off (0x01).
        _controlTransport->SendPacket(Aap::BuildHandshakePacket());

        // Fallback timer (220 ms) in case the L2CAP channel was already handshaked by an earlier
        // app session and AirPods do not send a fresh Handshake ACK.
        QMetaObject::invokeMethod(
            this,
            [this, connEpoch, resumeHeartRate] {
                QTimer::singleShot(220, this, [this, connEpoch, resumeHeartRate] {
                    CompleteAapPostHandshake(connEpoch, resumeHeartRate);
                });
            },
            Qt::QueuedConnection);
    }
    else {
        LOG(Info, "AAP control transport disconnected; falling back to BLE advertisements.");
        QMetaObject::invokeMethod(
            this,
            [this] {
                _heartRateWatchdog.stop();
                _convSpeechSafetyTimer.stop();
            },
            Qt::QueuedConnection);
        emit ConversationAwarenessLevelChanged(0);
    }

    EmitStateSignals(pending);
    emit ControlConnectionChanged(connected);
    ScheduleExpiryTimerLater();
    ReconcileScannerLater();
}

void Manager::CompleteAapPostHandshake(uint64_t connEpoch, bool resumeHeartRate)
{
    uint64_t initialHrEpoch = 0;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_aapConnected || _aapConnectionEpoch != connEpoch || _aapFeaturesEnabledSent ||
            !_controlTransport->IsConnected())
        {
            return;
        }
        _aapFeaturesEnabledSent = true;
        initialHrEpoch = _heartRateEpoch;
    }

    LOG(Info, "AAP handshake acknowledged; enabling H2 features (0x4D) and notifications (0x0F).");
    _controlTransport->SendPacket(Aap::BuildEnableFeaturesPacket());

    QMetaObject::invokeMethod(
        this,
        [this, connEpoch, initialHrEpoch, resumeHeartRate] {
            QTimer::singleShot(100, this, [this, connEpoch] {
                {
                    std::lock_guard<std::mutex> lock{_mutex};
                    if (!_aapConnected || _aapConnectionEpoch != connEpoch ||
                        !_controlTransport->IsConnected())
                    {
                        return;
                    }
                }
                _controlTransport->SendPacket(Aap::BuildRequestNotificationsPacket());
            });
            // Proactively arm Allow "Off" Listening Mode (0x34) once per session on models that
            // gate Off behind it (AirPods Pro 2/3, AirPods 4 ANC, AirPods 5) unless disabled or
            // already reported enabled by the AirPods.
            QTimer::singleShot(650, this, [this, connEpoch] {
                bool shouldArmAllowOff = false;
                PendingStateSignals armPending;
                {
                    std::lock_guard<std::mutex> lock{_mutex};
                    if (!_aapConnected || _aapConnectionEpoch != connEpoch ||
                        _userDisabledAllowOff || _allowOffSentToDevice ||
                        !_controlTransport->IsConnected())
                    {
                        return;
                    }
                    const auto st = _stateMgr.GetCurrentState();
                    if (st.has_value() &&
                        st->noiseControl.allowOffListeningMode == std::optional{true}) {
                        _allowOffSentToDevice = true;
                        return;
                    }
                    const Model model =
                        st.has_value() && st->model != Model::Unknown ? st->model : _boundModel;
                    if (CapabilitiesFor(model).supportsAllowOffMode) {
                        shouldArmAllowOff = true;
                        _allowOffSentToDevice = true;
                        if (auto u = _stateMgr.OnAapMessage(
                                Aap::FeatureToggleNotification{
                                    .feature = Aap::ToggleFeature::AllowOffListeningMode,
                                    .enabled = true},
                                _boundModel);
                            u.has_value())
                        {
                            armPending = PrepareStateChangedLocked(std::move(u.value()));
                        }
                    }
                }
                if (shouldArmAllowOff) {
                    LOG(Info, "AAP: arming Allow \"Off\" Listening Mode (0x34) for session.");
                    _controlTransport->SendPacket(Aap::BuildSetAllowOffListeningModePacket(true));
                    EmitStateSignals(armPending);
                }
            });
            // If initial listening mode / metadata has not arrived within 1.6 s (e.g. AirPods
            // were still settling A2DP when the first 0x0F arrived), send one clean retry of 0x0F
            // so Controls populate immediately instead of waiting for the 30 s periodic dump.
            QTimer::singleShot(1600, this, [this, connEpoch] {
                bool needRetry = false;
                {
                    std::lock_guard<std::mutex> lock{_mutex};
                    if (!_aapConnected || _aapConnectionEpoch != connEpoch ||
                        !_controlTransport->IsConnected())
                    {
                        return;
                    }
                    const auto st = _stateMgr.GetCurrentState();
                    needRetry = !st.has_value() || !st->noiseControl.activeMode.has_value();
                }
                if (needRetry) {
                    LOG(Info, "AAP: initial listening mode not yet received; re-requesting "
                              "notifications (0x0F).");
                    _controlTransport->SendPacket(Aap::BuildRequestNotificationsPacket());
                }
            });
            if (resumeHeartRate) {
                QTimer::singleShot(850, this, [this, connEpoch, initialHrEpoch] {
                    {
                        std::lock_guard<std::mutex> lock{_mutex};
                        if (!_aapConnected || !_heartRateWanted || _heartRatePausedOutOfEar ||
                            _aapConnectionEpoch != connEpoch || _heartRateEpoch != initialHrEpoch)
                        {
                            return;
                        }
                    }
                    StartHeartRateCampaignLater(true);
                });
            }
        },
        Qt::QueuedConnection);
}

void Manager::OnControlPacketReceived(std::span<const uint8_t> packet)
{
    if (IsHandshakeAckFrame(packet)) {
        uint64_t connEpoch = 0;
        bool resumeHr = false;
        bool triggerPostHandshake = false;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            if (_aapConnected && !_aapFeaturesEnabledSent) {
                connEpoch = _aapConnectionEpoch;
                resumeHr = _heartRateWanted;
                triggerPostHandshake = true;
            }
        }
        if (triggerPostHandshake) {
            CompleteAapPostHandshake(connEpoch, resumeHr);
        }
        return;
    }

    // 1. Feed every packet through the RTBuddy 0x17 SensorDataWX reassembler & stateful parser.
    bool mappingChanged = false;
    std::optional<uint8_t> discoveredHeart;
    std::optional<uint8_t> discoveredMotion;
    bool restartOnNewService = false;
    bool sawLockedSample = false;
    bool logWarmup = false;
    std::optional<RtBuddy::HeartRateSample> lockedSample;
    bool logBpm = false;
    PendingStateSignals rtPending;

    {
        std::lock_guard<std::mutex> lock{_mutex};
        const auto frames = _rtBuddyReassembler.Feed(packet);
        for (const auto &frame : frames) {
            const auto batch = _rtBuddyParser.ParseFrame(frame);
            if (batch.mappingChanged) {
                mappingChanged = true;
                discoveredHeart = _rtBuddyParser.HeartRateService();
                discoveredMotion = _rtBuddyParser.MotionService();
                restartOnNewService =
                    _heartRateWanted && !_heartRatePausedOutOfEar && _aapConnected;
            }
            if (batch.heartRate.has_value() && _deviceConnected) {
                const auto sample = batch.heartRate.value();
                _lastHeartRateSampleTime = std::chrono::steady_clock::now();
                sawLockedSample = true;
                lockedSample = sample;
                if (!_lastLoggedBpm.has_value() || _lastLoggedBpm.value() != sample.bpm) {
                    _lastLoggedBpm = sample.bpm;
                    logBpm = true;
                }
                if (auto update = _stateMgr.OnAapMessage(
                        Aap::HeartRateNotification{
                            .bpm = sample.bpm, .confidence = sample.confidence},
                        _boundModel);
                    update.has_value())
                {
                    rtPending = PrepareStateChangedLocked(std::move(update.value()));
                }
            }
            else if (batch.warmupFrameSeen && _heartRateWanted && _deviceConnected) {
                const auto cur = _stateMgr.GetCurrentState();
                const auto status =
                    cur.has_value() ? cur->noiseControl.heartRateStatus : HeartRateStatus::Off;
                if (status != HeartRateStatus::Live && status != HeartRateStatus::AcquiringLock) {
                    if (!_heartRateWarmupLogged) {
                        _heartRateWarmupLogged = true;
                        logWarmup = true;
                    }
                    if (auto update = _stateMgr.OnAapMessage(
                            Aap::HeartRateStatusNotification{
                                .status = HeartRateStatus::AcquiringLock},
                            _boundModel);
                        update.has_value())
                    {
                        rtPending = PrepareStateChangedLocked(std::move(update.value()));
                    }
                }
            }
        }
    }

    if (mappingChanged) {
        LOG(Info, "RTBuddy sensor services discovered: heart={}, motion={}",
            discoveredHeart.has_value() ? std::to_string(discoveredHeart.value()) : "none",
            discoveredMotion.has_value() ? std::to_string(discoveredMotion.value()) : "none");
        if (restartOnNewService) {
            UpdateHeartRateStatus(HeartRateStatus::Starting);
            StartHeartRateCampaignLater(true);
        }
    }
    if (logWarmup) {
        LOG(Info, "AAP optical heart rate: PPG sensor warming up, acquiring pulse lock...");
    }
    if (logBpm && lockedSample.has_value()) {
        const int confPct = (static_cast<int>(lockedSample->confidence) * 100 + 127) / 255;
        LOG(Info, "AAP optical heart rate: {} BPM (confidence {}%)", lockedSample->bpm, confPct);
    }
    if (sawLockedSample) {
        QMetaObject::invokeMethod(
            this,
            [this] {
                if (_heartRateWanted && _aapConnected) {
                    _heartRateWatchdog.start(12000);
                }
            },
            Qt::QueuedConnection);
    }
    EmitStateSignals(rtPending);

    if (IsRtBuddyFrame(packet)) {
        return;
    }

    // 2. Standard AAP control / status frames.
    const auto message = Aap::DecodePacket(packet);
    if (!message.has_value()) {
        if (packet.size() >= 6) {
            const uint8_t opcode = packet[4];
            bool firstTime = false;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                firstTime = _seenUnknownAapOpcodes.insert(opcode).second;
            }
            if (firstTime) {
                std::string hex;
                const size_t n = std::min<size_t>(packet.size(), 20);
                hex.reserve(n * 3);
                for (size_t i = 0; i < n; ++i) {
                    if (i > 0) {
                        hex.push_back(' ');
                    }
                    hex += std::format("{:02X}", packet[i]);
                }
                LOG(Trace, "AAP unhandled frame (len={}, opcode=0x{:02X}): {}", packet.size(),
                    opcode, hex);
            }
        }
        return;
    }

    LogDecodedAapMessage(message.value());

    std::optional<uint8_t> convLevelToEmit;
    std::optional<uint8_t> flushAdaptiveLevel;
    std::optional<NoiseControlMode> nextQueuedNoiseMode;
    bool pauseHrForEar = false;
    bool resumeHrForEar = false;
    bool autoStartHr = false;
    uint64_t autoStartConnEpoch = 0;
    uint64_t autoStartHrEpoch = 0;
    bool stopHrWatchdogExternal = false;
    bool retryOffMode = false;
    uint64_t retryOffEpoch = 0;
    bool retryAdaptiveMode = false;
    uint64_t retryAdaptiveEpoch = 0;
    uint8_t retryCycleMask = ListeningCycleBit::kOff | ListeningCycleBit::kNoiseCancellation |
                             ListeningCycleBit::kTransparency | ListeningCycleBit::kAdaptive;
    PendingStateSignals pending;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_deviceConnected) {
            return;
        }
        bool suppressNoiseEcho = false;
        std::optional<HeartRateStatus> adoptedHrStatus;
        if (const auto *toggleMsg = std::get_if<Aap::FeatureToggleNotification>(&message.value());
            toggleMsg != nullptr && toggleMsg->feature == Aap::ToggleFeature::HeartRateMonitoring)
        {
            if (toggleMsg->enabled) {
                if (_heartRateUserDisabled) {
                    // User explicitly disabled Heart Rate Monitoring in this session; ignore
                    // stale or periodic hardware 0x30 = 1 echoes so the toggle stays OFF.
                    suppressNoiseEcho = true;
                }
                else if (!_heartRateWanted && _aapConnected) {
                    // AirPods Pro 3 enable 0x30 (Heart Rate Monitoring) by default in firmware.
                    // Adopt the hardware's ON state immediately and kick off the RTBuddy optical
                    // PPG sequence so live BPM starts without requiring a manual off/on toggle.
                    const auto curSt = _stateMgr.GetCurrentState();
                    const Model model = curSt.has_value() && curSt->model != Model::Unknown
                                            ? curSt->model
                                            : _boundModel;
                    if (model == Model::Unknown || CapabilitiesFor(model).supportsHeartRate) {
                        const bool worn = curSt.has_value() &&
                                          (curSt->pods.left.isInEar || curSt->pods.right.isInEar);
                        const bool bothInCase =
                            curSt.has_value() && curSt->caseBox.isBothPodsInCase && !worn;
                        const bool notWorn = bothInCase || (_aapEarStateReceived && !worn);
                        _heartRateWanted = true;
                        _heartRateRetrying = false;
                        _heartRateWarmupLogged = false;
                        _lastLoggedBpm.reset();
                        _heartRatePausedOutOfEar = notWorn;
                        adoptedHrStatus =
                            notWorn ? HeartRateStatus::WaitingForEar : HeartRateStatus::Starting;
                        autoStartHr = !notWorn;
                        autoStartConnEpoch = _aapConnectionEpoch;
                        autoStartHrEpoch = _heartRateEpoch;
                    }
                }
            }
            else {
                if (_heartRateWanted && (_heartRatePausedOutOfEar || _heartRateRetrying)) {
                    // Suppress transient 0x30 = 0 echoes caused by out-of-ear pause or a
                    // watchdog optical sensor reset.
                    suppressNoiseEcho = true;
                }
                else if (_heartRateWanted) {
                    _heartRateWanted = false;
                    _heartRatePausedOutOfEar = false;
                    _heartRateRetrying = false;
                    ++_heartRateEpoch;
                    stopHrWatchdogExternal = true;
                }
            }
        }
        if (const auto *ncMsg = std::get_if<Aap::NoiseControlNotification>(&message.value())) {
            const auto now = std::chrono::steady_clock::now();
            if (ncMsg->mode == NoiseControlMode::Adaptive) {
                _pendingAdaptiveUntil = {};
                _adaptiveRetrySent = false;
                if (_pendingAdaptiveLevelToSend.has_value()) {
                    flushAdaptiveLevel = _pendingAdaptiveLevelToSend;
                    _pendingAdaptiveLevelToSend.reset();
                }
            }
            if (ncMsg->mode == NoiseControlMode::Off) {
                _pendingOffModeUntil = {};
                _pendingOffModeSendAt = {};
                _offModeRetrySent = false;
                if (_pendingAdaptiveUntil != std::chrono::steady_clock::time_point{} &&
                    now < _pendingAdaptiveUntil && !_adaptiveRetrySent)
                {
                    // AirPods rejected Adaptive and fell back to Off because H2 features (0x4D)
                    // had not settled yet. Re-send 0x4D and retry Adaptive once after 180 ms.
                    _adaptiveRetrySent = true;
                    retryAdaptiveEpoch = ++_pendingAdaptiveEpoch;
                    _pendingAdaptiveUntil = now + std::chrono::milliseconds{1100};
                    retryAdaptiveMode = true;
                    suppressNoiseEcho = true;
                }
            }
            else if (
                _pendingOffModeUntil != std::chrono::steady_clock::time_point{} &&
                now < _pendingOffModeUntil)
            {
                if (now < _pendingOffModeSendAt) {
                    // Ignore a stale mode echo arriving while waiting for 0x34 to settle
                    // before 0x0D = Off is sent.
                    suppressNoiseEcho = true;
                }
                else if (!_offModeRetrySent) {
                    // AirPods rejected Off (e.g. 0x34 or 0x1A cycle mask had not settled yet).
                    // Re-arm both and retry Off once after 200 ms.
                    _offModeRetrySent = true;
                    _allowOffSentToDevice = true;
                    retryOffEpoch = ++_pendingOffModeEpoch;
                    _pendingOffModeSendAt = now + std::chrono::milliseconds{200};
                    _pendingOffModeUntil = now + std::chrono::milliseconds{1200};
                    if (const auto st = _stateMgr.GetCurrentState();
                        st.has_value() && st->noiseControl.listeningModeCycleMask.has_value())
                    {
                        retryCycleMask = static_cast<uint8_t>(
                            st->noiseControl.listeningModeCycleMask.value() |
                            ListeningCycleBit::kOff);
                    }
                    retryOffMode = true;
                    suppressNoiseEcho = true;
                }
                else {
                    // Hardware rejected the retry as well; accept the actual echoed mode.
                    _pendingOffModeUntil = {};
                    _pendingOffModeSendAt = {};
                    _noiseModeInFlight = false;
                    ++_noiseModeInFlightEpoch;
                }
            }
        }
        if (!suppressNoiseEcho) {
            if (const auto *ncEcho = std::get_if<Aap::NoiseControlNotification>(&message.value())) {
                if (_noiseModeInFlight && ncEcho->mode == _inFlightNoiseMode) {
                    _noiseModeInFlight = false;
                    ++_noiseModeInFlightEpoch;
                    if (_queuedNoiseMode.has_value()) {
                        const NoiseControlMode queued = _queuedNoiseMode.value();
                        _queuedNoiseMode.reset();
                        if (queued != ncEcho->mode) {
                            nextQueuedNoiseMode = queued;
                            _lastRequestedNoiseMode.reset();
                            suppressNoiseEcho = true;
                        }
                    }
                }
                else if (_noiseModeInFlight) {
                    // Ignore a stale echo from an earlier mode while our new target is in flight.
                    suppressNoiseEcho = true;
                }
                if (!suppressNoiseEcho) {
                    _lastRequestedNoiseMode = ncEcho->mode;
                }
            }
        }
        if (!suppressNoiseEcho) {
            if (auto update = _stateMgr.OnAapMessage(message.value(), _boundModel);
                update.has_value())
            {
                pending = PrepareStateChangedLocked(std::move(update.value()));
            }
            if (adoptedHrStatus.has_value()) {
                if (auto u = _stateMgr.OnAapMessage(
                        Aap::HeartRateStatusNotification{.status = adoptedHrStatus.value()},
                        _boundModel);
                    u.has_value())
                {
                    pending = PrepareStateChangedLocked(std::move(u.value()));
                }
            }
        }

        if (const auto *speech =
                std::get_if<Aap::ConversationalAwarenessSpeechNotification>(&message.value()))
        {
            const auto st = _stateMgr.GetCurrentState();
            const bool caOff =
                st.has_value() && st->noiseControl.conversationalAwareness == std::optional{false};
            convLevelToEmit = caOff ? uint8_t{0} : speech->level;
        }
        else if (
            const auto *ca =
                std::get_if<Aap::ConversationalAwarenessNotification>(&message.value()))
        {
            if (!ca->enabled) {
                convLevelToEmit = uint8_t{0};
            }
        }
        else if (std::holds_alternative<Aap::EarDetectionNotification>(message.value())) {
            _aapEarStateReceived = true;
            const auto st = _stateMgr.GetCurrentState();
            const bool worn = st.has_value() && (st->pods.left.isInEar || st->pods.right.isInEar);
            if (!worn) {
                convLevelToEmit = uint8_t{0};
            }
            if (_heartRateWanted && _aapConnected) {
                if (!worn && !_heartRatePausedOutOfEar) {
                    _heartRatePausedOutOfEar = true;
                    ++_heartRateEpoch;
                    pauseHrForEar = true;
                    if (auto u = _stateMgr.OnAapMessage(
                            Aap::HeartRateStatusNotification{
                                .status = HeartRateStatus::WaitingForEar},
                            _boundModel);
                        u.has_value())
                    {
                        pending = PrepareStateChangedLocked(std::move(u.value()));
                    }
                }
                else if (
                    worn && (_heartRatePausedOutOfEar ||
                             (st.has_value() &&
                              (st->noiseControl.heartRateStatus == HeartRateStatus::WaitingForEar ||
                               st->noiseControl.heartRateStatus == HeartRateStatus::NoSignal))))
                {
                    _heartRatePausedOutOfEar = false;
                    resumeHrForEar = true;
                    if (auto u = _stateMgr.OnAapMessage(
                            Aap::HeartRateStatusNotification{.status = HeartRateStatus::Starting},
                            _boundModel);
                        u.has_value())
                    {
                        pending = PrepareStateChangedLocked(std::move(u.value()));
                    }
                }
            }
        }
    }

    EmitStateSignals(pending);

    if (flushAdaptiveLevel.has_value()) {
        _controlTransport->SendPacket(
            Aap::BuildSetAdaptiveNoiseLevelPacket(flushAdaptiveLevel.value()));
    }

    if (nextQueuedNoiseMode.has_value()) {
        const NoiseControlMode target = nextQueuedNoiseMode.value();
        QMetaObject::invokeMethod(
            this,
            [this, target] {
                QTimer::singleShot(40, this, [this, target] { SetNoiseControlMode(target); });
            },
            Qt::QueuedConnection);
    }

    if (retryOffMode) {
        LOG(Info,
            "AAP: AirPods bounced back from Listening Off; re-arming 0x34 + 0x1A (mask=0x{:02X}) "
            "and retrying Off in 200 ms.",
            retryCycleMask);
        _controlTransport->SendPacket(Aap::BuildSetAllowOffListeningModePacket(true));
        _controlTransport->SendPacket(Aap::BuildSetListeningModeCyclePacket(retryCycleMask));
        QMetaObject::invokeMethod(
            this,
            [this, retryOffEpoch] {
                QTimer::singleShot(200, this, [this, retryOffEpoch] {
                    {
                        std::lock_guard<std::mutex> lock{_mutex};
                        if (!_aapConnected || _pendingOffModeEpoch != retryOffEpoch ||
                            !_controlTransport->IsConnected())
                        {
                            return;
                        }
                    }
                    _controlTransport->SendPacket(
                        Aap::BuildSetNoiseControlPacket(NoiseControlMode::Off));
                });
            },
            Qt::QueuedConnection);
    }

    if (retryAdaptiveMode) {
        LOG(Info, "AAP: AirPods fell back to Off from Adaptive; re-sending H2 features (0x4D) and "
                  "retrying Adaptive in 180 ms.");
        _controlTransport->SendPacket(Aap::BuildEnableFeaturesPacket());
        QMetaObject::invokeMethod(
            this,
            [this, retryAdaptiveEpoch] {
                QTimer::singleShot(180, this, [this, retryAdaptiveEpoch] {
                    {
                        std::lock_guard<std::mutex> lock{_mutex};
                        if (!_aapConnected || _pendingAdaptiveEpoch != retryAdaptiveEpoch ||
                            !_controlTransport->IsConnected())
                        {
                            return;
                        }
                    }
                    _controlTransport->SendPacket(
                        Aap::BuildSetNoiseControlPacket(NoiseControlMode::Adaptive));
                });
            },
            Qt::QueuedConnection);
    }

    if (convLevelToEmit.has_value()) {
        ArmConversationSpeechSafetyLater(convLevelToEmit.value());
        emit ConversationAwarenessLevelChanged(convLevelToEmit.value());
    }
    if (stopHrWatchdogExternal) {
        QMetaObject::invokeMethod(
            this, [this] { _heartRateWatchdog.stop(); }, Qt::QueuedConnection);
    }
    if (pauseHrForEar) {
        LOG(Info, "AAP heart rate: AirPods removed from ears; pausing optical sensor.");
        QMetaObject::invokeMethod(
            this, [this] { _heartRateWatchdog.stop(); }, Qt::QueuedConnection);
        StopHeartRateWire(false);
    }
    else if (resumeHrForEar) {
        LOG(Info, "AAP heart rate: ear contact detected; resuming optical sensor.");
        StartHeartRateCampaignLater(true);
    }
    else if (autoStartHr) {
        LOG(Info,
            "AAP heart rate: AirPods reported heart rate monitoring enabled; starting optical PPG "
            "sensor.");
        // Wait 350 ms so the initial 0x0F burst (including EarDetectionNotification) settles
        // before opening RTBuddy services. If buds are out of ear, EarDetectionNotification
        // bumps `_heartRateEpoch` during this window and transitions cleanly to WaitingForEar.
        QMetaObject::invokeMethod(
            this,
            [this, autoStartConnEpoch, autoStartHrEpoch] {
                QTimer::singleShot(350, this, [this, autoStartConnEpoch, autoStartHrEpoch] {
                    {
                        std::lock_guard<std::mutex> lock{_mutex};
                        if (!_heartRateWanted || _heartRatePausedOutOfEar || !_aapConnected ||
                            _aapConnectionEpoch != autoStartConnEpoch ||
                            _heartRateEpoch != autoStartHrEpoch ||
                            !_controlTransport->IsConnected())
                        {
                            return;
                        }
                    }
                    StartHeartRateCampaignLater(true);
                });
            },
            Qt::QueuedConnection);
    }
}

uint8_t Manager::NextRtBuddySeqLocked()
{
    _rtBuddySeq = static_cast<uint8_t>((_rtBuddySeq % 127u) + 1u);
    return _rtBuddySeq;
}

void Manager::StartHeartRateCampaignLater(bool resetAttempts)
{
    uint64_t epoch = 0;
    int attempt = 1;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_heartRateWanted || !_aapConnected || !_controlTransport->IsConnected()) {
            return;
        }
        epoch = ++_heartRateEpoch;
        _heartRateAttempt = resetAttempts ? 1 : (_heartRateAttempt + 1);
        attempt = _heartRateAttempt;
        _lastHeartRateSampleTime = {};
        _heartRateWarmupLogged = false;
    }

    QMetaObject::invokeMethod(
        this, [this, epoch, attempt] { RunHeartRateAttempt(epoch, attempt); },
        Qt::QueuedConnection);
}

void Manager::RunHeartRateAttempt(uint64_t epoch, int attempt)
{
    std::vector<uint8_t> services;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_heartRateWanted || !_aapConnected || _heartRateEpoch != epoch ||
            !_controlTransport->IsConnected())
        {
            return;
        }
        services = _rtBuddyParser.TargetHeartRateServices();
    }

    std::string serviceList;
    for (size_t i = 0; i < services.size(); ++i) {
        if (i > 0) {
            serviceList.push_back(',');
        }
        serviceList += std::to_string(services[i]);
    }
    LOG(Info, "AAP heart rate: starting optical PPG sequence (attempt {}/3, services={}).", attempt,
        serviceList);

    auto sendIfCurrent = [this, epoch](std::vector<uint8_t> pkt) {
        {
            std::lock_guard<std::mutex> lock{_mutex};
            if (!_heartRateWanted || !_aapConnected || _heartRateEpoch != epoch ||
                !_controlTransport->IsConnected())
            {
                return;
            }
        }
        _controlTransport->SendPacket(pkt);
    };

    // Exact Apple AirPods Pro 3 RTBuddy open-services + descriptor discovery + HRM_STATE(0x30)=1
    // + SensorDataWX stream sequence with captured inter-packet timings.
    sendIfCurrent(RtBuddy::BuildConnectService0Packet());

    QTimer::singleShot(
        180, this, [sendIfCurrent] { sendIfCurrent(RtBuddy::BuildCapabilitiesService0Packet()); });
    QTimer::singleShot(
        400, this, [sendIfCurrent] { sendIfCurrent(RtBuddy::BuildConnectService4Packet()); });
    QTimer::singleShot(
        580, this, [sendIfCurrent] { sendIfCurrent(RtBuddy::BuildCapabilitiesService4Packet()); });
    QTimer::singleShot(680, this, [this, epoch] {
        std::vector<uint8_t> req;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            if (!_heartRateWanted || !_aapConnected || _heartRateEpoch != epoch ||
                !_controlTransport->IsConnected())
            {
                return;
            }
            req = RtBuddy::BuildRequestAllDescriptorsPacket(NextRtBuddySeqLocked());
        }
        _controlTransport->SendPacket(req);
    });
    QTimer::singleShot(800, this, [this, epoch] {
        {
            std::lock_guard<std::mutex> lock{_mutex};
            if (!_heartRateWanted || !_aapConnected || _heartRateEpoch != epoch ||
                !_controlTransport->IsConnected())
            {
                return;
            }
            _heartRateRetrying = false;
        }
        _controlTransport->SendPacket(RtBuddy::BuildHrmStatePacket(true));
    });

    int delayMs = 920;
    for (const uint8_t svc : services) {
        QTimer::singleShot(delayMs, this, [this, epoch, svc] {
            std::vector<uint8_t> pkt;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                if (!_heartRateWanted || !_aapConnected || _heartRateEpoch != epoch ||
                    !_controlTransport->IsConnected())
                {
                    return;
                }
                pkt = RtBuddy::BuildServiceReportIntervalPacket(
                    NextRtBuddySeqLocked(), svc, RtBuddy::kDefaultReportPeriodUs);
            }
            _controlTransport->SendPacket(pkt);
        });
        delayMs += 120;
    }

    // Arm the 12-second watchdog once the stream start packets have gone out.
    _heartRateWatchdog.start(13200);
}

void Manager::StopHeartRateWire(bool disableHrmToggle)
{
    std::vector<std::vector<uint8_t>> packets;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_aapConnected || !_controlTransport->IsConnected()) {
            return;
        }
        for (const uint8_t svc : _rtBuddyParser.TargetHeartRateServices()) {
            packets.push_back(
                RtBuddy::BuildServiceReportIntervalPacket(NextRtBuddySeqLocked(), svc, 0));
        }
        if (disableHrmToggle) {
            packets.push_back(RtBuddy::BuildHrmStatePacket(false));
        }
    }

    for (const auto &pkt : packets) {
        _controlTransport->SendPacket(pkt);
    }
}

void Manager::UpdateHeartRateStatus(HeartRateStatus status)
{
    PendingStateSignals pending;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_deviceConnected) {
            return;
        }
        if (auto u = _stateMgr.OnAapMessage(
                Aap::HeartRateStatusNotification{.status = status}, _boundModel);
            u.has_value())
        {
            pending = PrepareStateChangedLocked(std::move(u.value()));
        }
    }
    EmitStateSignals(pending);
}

void Manager::OnHeartRateWatchdogTimeout()
{
    constexpr int kMaxAttempts = 3;
    int attempt = 0;
    bool stillValid = false;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_heartRateWanted || _heartRatePausedOutOfEar || !_aapConnected ||
            !_controlTransport->IsConnected())
        {
            return;
        }
        const auto now = std::chrono::steady_clock::now();
        if (_lastHeartRateSampleTime != std::chrono::steady_clock::time_point{} &&
            now - _lastHeartRateSampleTime < std::chrono::seconds{11})
        {
            _heartRateWatchdog.start(12000);
            return;
        }
        stillValid = true;
        attempt = _heartRateAttempt;
    }

    if (!stillValid) {
        return;
    }

    if (attempt < kMaxAttempts) {
        {
            std::lock_guard<std::mutex> lock{_mutex};
            _heartRateRetrying = true;
        }
        StopHeartRateWire(true);
        LOG(Warn, "AAP heart rate: no locked optical sample within 12 s; retrying ({}/{}).",
            attempt + 1, kMaxAttempts);
        UpdateHeartRateStatus(HeartRateStatus::Starting);
        QTimer::singleShot(350, this, [this] { StartHeartRateCampaignLater(false); });
    }
    else {
        {
            std::lock_guard<std::mutex> lock{_mutex};
            _heartRateRetrying = false;
        }
        StopHeartRateWire(false);
        LOG(Warn, "AAP heart rate: no optical pulse lock after {} attempts; pausing sensor.",
            kMaxAttempts);
        UpdateHeartRateStatus(HeartRateStatus::NoSignal);
    }
}

void Manager::ArmConversationSpeechSafetyLater(uint8_t level)
{
    QMetaObject::invokeMethod(
        this,
        [this, level] {
            if (level == 0 || level >= 8) {
                _convSpeechSafetyTimer.stop();
            }
            else if (level <= 2) {
                // Safety fallback if a disconnect or RF glitch drops the recovery packet.
                _convSpeechSafetyTimer.start(15000);
            }
            else {
                // AirPods often stop at an intermediate recovery level (3..7) without sending 8/9.
                _convSpeechSafetyTimer.start(2200);
            }
        },
        Qt::QueuedConnection);
}

void Manager::OnConversationSpeechSafetyTimeout()
{
    PendingStateSignals pending;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_deviceConnected) {
            return;
        }
        if (auto u = _stateMgr.OnAapMessage(
                Aap::ConversationalAwarenessSpeechNotification{.speaking = false, .level = 9},
                _boundModel);
            u.has_value())
        {
            pending = PrepareStateChangedLocked(std::move(u.value()));
        }
    }
    EmitStateSignals(pending);
    emit ConversationAwarenessLevelChanged(0);
}

auto Manager::PrepareStateChangedLocked(Details::StateManager::UpdateEvent updateEvent)
    -> PendingStateSignals
{
    const auto &oldState = updateEvent.oldState;
    auto &newState = updateEvent.newState;

    if (newState.displayName.isEmpty()) {
        newState.displayName = Details::ResolveDisplayName(_deviceName, newState.model);
    }

    PendingStateSignals pending;
    pending.state = newState;

    const auto isLidOpenForPopup = [](const State &st) {
        if (!st.caseBox.isLidOpened) {
            return false;
        }
        if (st.batteryPrecision == BatteryPrecision::ExactPercent) {
            // Over AAP, Case battery only becomes active while the charging case lid is open.
            return true;
        }
        return st.caseBox.isBothPodsInCase || st.caseBox.battery.Available();
    };

    const bool newLidOpened = isLidOpenForPopup(newState);
    bool lidStateSwitched;
    if (!oldState.has_value()) {
        lidStateSwitched = newLidOpened;
    }
    else {
        const bool oldLidOpened = isLidOpenForPopup(oldState.value());
        lidStateSwitched = oldLidOpened != newLidOpened;
    }
    if (lidStateSwitched) {
        pending.lidToggled = newLidOpened;
    }
    return pending;
}

void Manager::EmitStateSignals(const PendingStateSignals &pending)
{
    if (pending.state.has_value()) {
        emit StateUpdated(pending.state.value());
    }
    if (pending.lidToggled.has_value()) {
        emit LidToggled(pending.lidToggled.value());
    }
}

bool Manager::OnAdvertisementReceived(const Bluetooth::AdvertisementWatcher::ReceivedData &data)
{
    if (!Details::Advertisement::IsDesiredAdv(data)) {
        return false;
    }

    Details::Advertisement adv{data};
    PendingStateSignals pendingState;
    PendingNearbySignals pendingNearby;
    bool handledConnected = false;

    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_deviceConnected) {
            if (_scanWhileDisconnected || _showNearbyWhenDisconnected || _showPopupOnLidOpen) {
                pendingNearby = OnAdvertisementWhileDisconnectedLocked(adv);
            }
            else {
                LOG(Trace, "AirPods advertisement received, but device disconnected.");
            }
        }
        else if (_boundModel != Model::Unknown && adv.GetAdvState().model != _boundModel) {
            LOG(Trace, "Ignore advertisement for a model other than the bound device.");
        }
        else {
            _lastRssi = data.rssi;
            _lastAdvHex = Helper::ToString(adv.GetDesensitizedData());
            LOG(Trace, "AirPods advertisement received. Data: {}, Address Hash: {}, RSSI: {}",
                _lastAdvHex.toStdString(), Helper::Hash(data.address), data.rssi);

            if (auto optUpdateEvent = _stateMgr.OnAdvReceived(std::move(adv));
                optUpdateEvent.has_value())
            {
                pendingState = PrepareStateChangedLocked(std::move(optUpdateEvent.value()));
            }
            handledConnected = true;
        }
    }

    if (pendingNearby.lidOpenedNearby) {
        emit LidOpenedNearby();
    }
    if (pendingNearby.nearbyState.has_value()) {
        emit NearbyStateUpdated(pendingNearby.nearbyState.value(), pendingNearby.shouldAutoShow);
    }
    if (pendingNearby.scheduleExpiry || handledConnected) {
        ScheduleExpiryTimerLater();
    }
    EmitStateSignals(pendingState);
    return handledConnected;
}

auto Manager::OnAdvertisementWhileDisconnectedLocked(const Details::Advertisement &adv)
    -> PendingNearbySignals
{
    PendingNearbySignals out;

    const auto &advState = adv.GetAdvState();
    if (_boundModel != Model::Unknown && advState.model != _boundModel) {
        return out;
    }
    if (adv.GetRssi() < _rssiMin) {
        return out;
    }

    // Bit 3 of the lid byte is only meaningful when the case is participating in the
    // broadcast (both buds in case or case battery reported). While both buds are worn out of
    // the case, Apple clears the bit and sets case battery to 0x0F (unavailable).
    const bool caseLidOpen = advState.caseBox.isLidOpened && (advState.caseBox.isBothPodsInCase ||
                                                              advState.caseBox.battery.Available());
    const bool lidCounterChanged = caseLidOpen && _lastNearbyLidCounter.has_value() &&
                                   _lastNearbyLidCounter.value() != advState.lidOpenCounter;

    if (_scanWhileDisconnected && caseLidOpen && advState.caseBox.isBothPodsInCase) {
        LOG(Trace, "The bound model's case is open nearby while disconnected. RSSI: {}",
            adv.GetRssi());
        out.lidOpenedNearby = true;
    }

    if (!_showNearbyWhenDisconnected && !(_showPopupOnLidOpen && caseLidOpen)) {
        return out;
    }

    State nearby = static_cast<const State &>(advState);
    nearby.isNearbyOtherDevice = true;
    nearby.caseBox.isLidOpened = caseLidOpen;
    nearby.displayName = Details::ResolveDisplayName(_deviceName, nearby.model);

    // Merge previously observed opposite-side battery when the broadcast only reports one side.
    if (_lastNearbyState.has_value() && _lastNearbyState->model == nearby.model) {
        if (!nearby.pods.left.battery.Available() &&
            _lastNearbyState->pods.left.battery.Available())
        {
            nearby.pods.left = _lastNearbyState->pods.left;
        }
        if (!nearby.pods.right.battery.Available() &&
            _lastNearbyState->pods.right.battery.Available())
        {
            nearby.pods.right = _lastNearbyState->pods.right;
        }
        if (!nearby.caseBox.battery.Available() && _lastNearbyState->caseBox.battery.Available() &&
            (caseLidOpen || nearby.caseBox.isBothPodsInCase))
        {
            nearby.caseBox.battery = _lastNearbyState->caseBox.battery;
            nearby.caseBox.isCharging = _lastNearbyState->caseBox.isCharging;
        }
    }

    const bool wasActive = _lastNearbyState.has_value();
    const bool lidJustOpened = (caseLidOpen && !_lastNearbyLidOpen) || lidCounterChanged;
    const bool stateChanged = !wasActive || _lastNearbyState.value() != nearby;

    _lastNearbySeen = std::chrono::steady_clock::now();
    _lastNearbyLidOpen = caseLidOpen;
    if (caseLidOpen) {
        _lastNearbyLidCounter = advState.lidOpenCounter;
    }
    _lastNearbyState = nearby;

    if (stateChanged || lidJustOpened) {
        out.nearbyState = nearby;
        out.shouldAutoShow = !wasActive || lidJustOpened;
    }

    out.scheduleExpiry = true;
    return out;
}

void Manager::OnAdvWatcherStateChanged(
    Bluetooth::AdvertisementWatcher::State state, const std::optional<std::string> &optError)
{
    std::optional<bool> availability;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        switch (state) {
        case Bluetooth::AdvertisementWatcher::State::Started:
            availability = true;
            LOG(Info, "Bluetooth AdvWatcher started.");
            break;

        case Bluetooth::AdvertisementWatcher::State::Stopped:
            if (_scannerWanted) {
                availability = false;
                LOG(Warn, "Bluetooth AdvWatcher stopped. Error: '{}'.",
                    optError.value_or("nullopt"));
            }
            else {
                LOG(Info, "Bluetooth AdvWatcher stopped intentionally.");
            }
            break;

        default:
            FatalError(
                std::format("Unhandled adv watcher state: '{}'", Helper::ToUnderlying(state)),
                true);
        }
    }

    if (availability.has_value()) {
        emit ScannerAvailabilityChanged(availability.value());
    }
}

std::vector<Bluetooth::Device> GetDevices()
{
    std::vector<Bluetooth::Device> devices =
        Bluetooth::DeviceManager::GetDevicesByState(Bluetooth::DeviceState::Paired);

    LOG(Info, "Paired devices count: {}", devices.size());

    devices.erase(
        std::remove_if(
            devices.begin(), devices.end(),
            [](const auto &device) {
                const auto vendorId = device.GetVendorId();
                const auto productId = device.GetProductId();
                const auto doErase = vendorId != AppleCP::VendorId ||
                                     AppleCP::AirPods::GetModel(productId) == Model::Unknown;

                LOG(Trace, "Device VendorId: '{}', ProductId: '{}', doErase: {}", vendorId,
                    productId, doErase);
                return doErase;
            }),
        devices.end());

    LOG(Info, "AirPods devices count: {} (filtered)", devices.size());
    return devices;
}

} // namespace Core::AirPods
