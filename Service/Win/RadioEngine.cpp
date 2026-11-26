// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/RadioEngine.h"
#include "Service/Win/RadioDriverManager.h"
#include "Service/Win/RegistryStore.h"
#include "Service/Win/ServiceLog.h"

#include <chrono>

namespace Service::Win {

namespace {

constexpr const char *kRadioBusId = "1-1";
constexpr const char *kMicBusId = "1-2";

LogLevel ToWinSeverity(Protocol::HciProxyEngine::LogLevel lvl)
{
    switch (lvl) {
    case Protocol::HciProxyEngine::LogLevel::Trace:
        return LogLevel::Trace;
    case Protocol::HciProxyEngine::LogLevel::Info:
        return LogLevel::Info;
    case Protocol::HciProxyEngine::LogLevel::Warn:
        return LogLevel::Warn;
    case Protocol::HciProxyEngine::LogLevel::Error:
        return LogLevel::Error;
    default:
        return LogLevel::Info;
    }
}

} // namespace

RadioEngine::RadioEngine() : _watchdog{RegistryStore::GetGuardTripped()}
{
    _hci.SetConfig(RegistryStore::LoadProxyConfig());
    _hci.SetDeferAapUntilAvdtp(true);
    _hci.SetPullModeForInjectedAcl(true);

    Protocol::HciProxyEngine::Callbacks cb{};
    cb.onInjectedAclAvailable = [this]() {
        std::shared_ptr<RadioBridgeSession> sess;
        {
            std::lock_guard<std::mutex> lock{_sessionMutex};
            sess = _radioSession;
        }
        if (sess) {
            sess->NotifyInjectedAclAvailable();
        }
    };
    cb.sendCommandToController = [this](std::vector<uint8_t> cmd) {
        std::shared_ptr<RadioBridgeSession> sess;
        {
            std::lock_guard<std::mutex> lock{_sessionMutex};
            sess = _radioSession;
        }
        if (sess) {
            sess->EnqueueInjectedCommand(std::move(cmd));
        }
    };
    cb.onAapConnectionChanged = [this](uint64_t addr, bool connected, uint32_t errCode) {
        const auto payload = Protocol::EncodeAapChannelState(Protocol::AapChannelStateMsg{
            .bluetoothAddress = addr,
            .connected = connected,
            .errorCode = errCode,
        });
        _pipeServer.Broadcast(Protocol::MessageType::AapChannelState, payload);
        BroadcastStatus();
    };
    cb.onAapPacketReceived = [this](uint64_t addr, std::vector<uint8_t> data) {
        const auto payload = Protocol::EncodeAapPacket(Protocol::AapPacketMsg{
            .bluetoothAddress = addr,
            .data = std::move(data),
        });
        _pipeServer.Broadcast(Protocol::MessageType::AapPacketReceived, payload);
    };
    cb.onCallAudioChanged = [this](uint64_t addr, bool active) {
        const auto payload = Protocol::EncodeCallAudioState(Protocol::CallAudioStateMsg{
            .bluetoothAddress = addr,
            .active = active,
        });
        _pipeServer.Broadcast(Protocol::MessageType::CallAudioState, payload);
        BroadcastStatus();
    };
    cb.onControllerTrouble = [this](std::string reason) {
        ServiceLog::Write(LogLevel::Warn, "hci", "Controller trouble: " + reason);
        std::shared_ptr<RadioBridgeSession> sess;
        {
            std::lock_guard<std::mutex> lock{_sessionMutex};
            sess = _radioSession;
        }
        if (sess) {
            sess->Stop();
        }
    };
    cb.onLog = [](Protocol::HciProxyEngine::LogLevel lvl, std::string msg) {
        ServiceLog::Write(ToWinSeverity(lvl), "hci", msg);
    };
    _hci.SetCallbacks(std::move(cb));
}

RadioEngine::~RadioEngine()
{
    Stop();
}

void RadioEngine::Start()
{
    if (_running.exchange(true)) {
        return;
    }

    SetState(Protocol::RadioServiceState::Starting, "Starting AirpodsBridge Radio Service.");

    _pipeServer.Start([this](uint64_t clientId, const Protocol::Frame &frame) {
        HandlePipeFrame(clientId, frame);
    });

    _usbIp.Start([this](const std::string &busId, UniqueSocket client) {
        HandleUsbIpImport(busId, std::move(client));
    });

    _supervisorThread = std::thread{&RadioEngine::SupervisorLoop, this};
}

void RadioEngine::Stop()
{
    if (!_running.exchange(false)) {
        return;
    }

    _supervisorCv.notify_all();
    _radioImportCv.notify_all();

    {
        std::shared_ptr<RadioBridgeSession> sess;
        {
            std::lock_guard<std::mutex> lock{_sessionMutex};
            sess = _radioSession;
        }
        if (sess) {
            sess->Stop();
        }
    }

    // Tear down virtual mic if attached.
    {
        std::unique_ptr<MicBridgeSession> micSess;
        int micPort = 0;
        {
            std::lock_guard<std::mutex> lock{_micMutex};
            _micWanted = false;
            DetachVirtualMicLocked(micSess, micPort);
        }
        if (micSess) {
            micSess->Stop();
        }
        if (_micThread.joinable()) {
            _micThread.join();
        }
        if (micPort > 0) {
            UsbIpServer::DetachLocalPort(micPort);
        }
    }

    if (_supervisorThread.joinable()) {
        _supervisorThread.join();
    }

    _usbIp.Stop();
    _radio.Close();
    _pipeServer.Stop();

    if (RegistryStore::GetUninstalling() || _restoreRequested.load()) {
        ServiceLog::Write(
            LogLevel::Info, "engine", "Restoring native Bluetooth driver during stop.");
        RadioDriverManager::RestoreAllBoundAdapters();
    }

    SetState(Protocol::RadioServiceState::Stopped, "Service stopped.");
}

void RadioEngine::OnSystemSuspend()
{
    _suspended.store(true);
    ServiceLog::Write(LogLevel::Info, "engine", "System suspending: pausing virtual radio.");
    std::shared_ptr<RadioBridgeSession> sess;
    {
        std::lock_guard<std::mutex> lock{_sessionMutex};
        sess = _radioSession;
    }
    if (sess) {
        sess->Stop();
    }
}

void RadioEngine::OnSystemResume()
{
    _suspended.store(false);
    ServiceLog::Write(LogLevel::Info, "engine", "System resumed: waking radio supervisor.");
    _supervisorCv.notify_all();
}

Protocol::StatusSnapshot RadioEngine::GetStatusSnapshot() const
{
    Protocol::StatusSnapshot snap{};
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        snap.state = _state;
        snap.usbipDriverReady = _usbipReady;
        snap.winUsbBound = _winUsbBound;
        snap.virtualAdapterAttached = _virtualRadioAttached;
        snap.guardTripped = _watchdog.IsGuardTripped();
        snap.recentFailures = _watchdog.RecentFailureCount(SteadyNowMs());
        snap.adapterVid = _activeAdapterVid;
        snap.adapterPid = _activeAdapterPid;
        snap.adapterInstanceId = WideToUtf8(_activeAdapterInstance);
        snap.adapterDisplayName = _activeAdapterDisplayName;
        snap.detail = _detail;
    }

    const auto cfg = _hci.GetConfig();
    snap.lowLatencyQosEnabled = cfg.lowLatencyQos;
    snap.activePollLatencyUs = cfg.lowLatencyQos ? cfg.pollLatencyUs : 25000u;
    snap.declineHfpEnabled = cfg.declineHfp;

    const auto tel = _hci.GetTelemetry();
    snap.hciManufacturer = tel.hciManufacturer;
    snap.activeAapChannels = tel.activeAapChannels;
    snap.callAudioActive = tel.callAudioActive;
    snap.injectedPackets = tel.injectedAclPackets;
    snap.interceptedPackets = tel.interceptedAclPackets;

    {
        std::lock_guard<std::mutex> lock{_micMutex};
        snap.virtualMicAttached = _micAttached;
        snap.virtualMicCapturing = _micEmulator ? _micEmulator->IsCapturing() : false;
    }

    return snap;
}

void RadioEngine::SupervisorLoop()
{
    while (_running.load()) {
        if (_suspended.load()) {
            std::unique_lock<std::mutex> lock{_stateMutex};
            _supervisorCv.wait_for(lock, std::chrono::seconds(2), [&] {
                return !_running.load() || !_suspended.load();
            });
            continue;
        }

        if (_restoreRequested.load()) {
            SetState(
                Protocol::RadioServiceState::RestoringNative,
                "Native Bluetooth driver restored by user request.");
            RadioDriverManager::RestoreAllBoundAdapters();
            std::unique_lock<std::mutex> lock{_stateMutex};
            _winUsbBound = false;
            _virtualRadioAttached = false;
            _supervisorCv.wait_for(lock, std::chrono::seconds(5), [&] {
                return !_running.load() || !_restoreRequested.load();
            });
            continue;
        }

        if (_watchdog.IsGuardTripped()) {
            SetState(
                Protocol::RadioServiceState::GuardTripped,
                "Safety guard tripped after repeated takeover failures; native Bluetooth driver "
                "restored.");
            std::unique_lock<std::mutex> lock{_stateMutex};
            _supervisorCv.wait_for(lock, std::chrono::seconds(5), [&] {
                return !_running.load() || !_watchdog.IsGuardTripped();
            });
            continue;
        }

        // Pre-flight check: never touch the physical Bluetooth driver unless signed usbip-win2
        // is verified installed and reachable.
        const bool usbipOk = RadioDriverManager::IsUsbipDriverReady();
        {
            std::lock_guard<std::mutex> lock{_stateMutex};
            _usbipReady = usbipOk;
        }
        if (!usbipOk) {
            SetState(
                Protocol::RadioServiceState::PreflightMissingUsbip,
                "Signed usbip-win2 driver is not installed. Run Setup-RadioService.ps1 to enable "
                "No-Test-Mode control.");
            std::unique_lock<std::mutex> lock{_stateMutex};
            _supervisorCv.wait_for(
                lock, std::chrono::seconds(10), [&] { return !_running.load(); });
            continue;
        }

        RunTakeoverCycle();
    }
}

void RadioEngine::RunTakeoverCycle()
{
    const auto search = RadioDriverManager::FindPhysicalAdapter();
    if (search.status == AdapterSearchStatus::NonUsbOnly) {
        SetState(
            Protocol::RadioServiceState::NonUsbAdapterOnly,
            "Bluetooth adapter is connected over UART/SDIO rather than USB and cannot be proxied.");
        std::unique_lock<std::mutex> lock{_stateMutex};
        _supervisorCv.wait_for(lock, std::chrono::seconds(15), [&] { return !_running.load(); });
        return;
    }
    if (search.status != AdapterSearchStatus::Found || !search.adapter.has_value()) {
        SetState(
            Protocol::RadioServiceState::NoUsbAdapter,
            "No physical USB Bluetooth adapter detected.");
        std::unique_lock<std::mutex> lock{_stateMutex};
        _supervisorCv.wait_for(lock, std::chrono::seconds(5), [&] { return !_running.load(); });
        return;
    }

    const AdapterCandidate adapter = *search.adapter;
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        _activeAdapterInstance = adapter.instanceId;
        _activeAdapterDisplayName =
            adapter.displayName.empty() ? "USB Bluetooth Adapter" : adapter.displayName;
        _activeAdapterVid = adapter.vid;
        _activeAdapterPid = adapter.pid;
    }
    RegistryStore::SetStoredRadioInstance(adapter.instanceId);

    // 1. Ensure physical adapter is bound to inbox WinUSB.
    if (!RadioDriverManager::IsOnWinUsb(adapter.instanceId)) {
        SetState(
            Protocol::RadioServiceState::BindingWinUsb,
            "Switching physical Bluetooth adapter to inbox WinUSB driver.");
        bool needReboot = false;
        if (!RadioDriverManager::BindWinUsb(adapter.instanceId, needReboot)) {
            const auto decision = _watchdog.RecordTakeoverFailure(SteadyNowMs());
            if (decision.action ==
                Protocol::WatchdogPolicy::FailureAction::TripGuardAndRestoreNative)
            {
                RegistryStore::SetGuardTripped(true);
                ServiceLog::ReportEventLogError(
                    "AirpodsBridgeService tripped safety guard after 3 WinUSB bind failures; "
                    "restoring native Bluetooth driver.");
                RadioDriverManager::RestoreAllBoundAdapters();
                BroadcastStatus();
                return;
            }
            Sleep(decision.backoffMs);
            return;
        }
        Sleep(400); // Allow WinUSB PnP interface to come online.
    }

    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        _winUsbBound = true;
    }

    // 2. Open WinUSB device handle & read descriptors.
    const std::wstring devPath = RadioDriverManager::DevicePathForWinUsb(adapter.instanceId);
    bool opened = false;
    for (int attempt = 0; attempt < 10 && _running.load(); ++attempt) {
        if (_radio.Open(devPath)) {
            opened = true;
            break;
        }
        Sleep(300);
    }

    if (!opened || !_radio.HciPipes().Valid()) {
        ServiceLog::Write(
            LogLevel::Error, "engine",
            "Failed to open WinUSB radio or locate HCI pipes; state=" +
                RadioDriverManager::DescribeDeviceState(adapter.instanceId));
        _radio.Close();
        const auto decision = _watchdog.RecordTakeoverFailure(SteadyNowMs());
        if (decision.action == Protocol::WatchdogPolicy::FailureAction::TripGuardAndRestoreNative) {
            RegistryStore::SetGuardTripped(true);
            ServiceLog::ReportEventLogError(
                "AirpodsBridgeService tripped safety guard after WinUSB open failures; restoring "
                "native Bluetooth driver.");
            RadioDriverManager::RestoreAllBoundAdapters();
            BroadcastStatus();
            return;
        }
        Sleep(decision.backoffMs);
        return;
    }

    // 3. Export physical radio over localhost USB/IP and attach via signed usbip-win2.
    _usbIp.ExportDevice(_radio.MakeExportedDevice(kRadioBusId));
    const auto portOpt = _usbIp.AttachLocalDevice(kRadioBusId);
    if (!portOpt.has_value()) {
        _usbIp.UnexportDevice(kRadioBusId);
        _radio.Close();
        const auto decision = _watchdog.RecordTakeoverFailure(SteadyNowMs());
        if (decision.action == Protocol::WatchdogPolicy::FailureAction::TripGuardAndRestoreNative) {
            RegistryStore::SetGuardTripped(true);
            ServiceLog::ReportEventLogError(
                "AirpodsBridgeService tripped safety guard after USB/IP attach failures; restoring "
                "native Bluetooth driver.");
            RadioDriverManager::RestoreAllBoundAdapters();
            BroadcastStatus();
            return;
        }
        Sleep(decision.backoffMs);
        return;
    }

    const int attachedPort = *portOpt;
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        _virtualRadioUsbipPort = attachedPort;
    }

    // Wait for usbip-win2 to import the TCP connection.
    UniqueSocket importedSocket;
    {
        std::unique_lock<std::mutex> lock{_sessionMutex};
        _radioImportCv.wait_for(lock, std::chrono::seconds(10), [&] {
            return !_running.load() || _pendingRadioSocket.Valid();
        });
        importedSocket = std::move(_pendingRadioSocket);
    }

    if (!importedSocket.Valid()) {
        UsbIpServer::DetachLocalPort(attachedPort);
        _usbIp.UnexportDevice(kRadioBusId);
        _radio.Close();
        const auto decision = _watchdog.RecordTakeoverFailure(SteadyNowMs());
        if (decision.action == Protocol::WatchdogPolicy::FailureAction::TripGuardAndRestoreNative) {
            RegistryStore::SetGuardTripped(true);
            RadioDriverManager::RestoreAllBoundAdapters();
            BroadcastStatus();
            return;
        }
        Sleep(decision.backoffMs);
        return;
    }

    // 4. Bridge is live!
    _watchdog.MarkSessionStarted(SteadyNowMs());
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        _virtualRadioAttached = true;
    }
    SetState(
        Protocol::RadioServiceState::Running,
        "No-Test-Mode Bluetooth HCI/L2CAP proxy active (WinUSB + signed usbip-win2).");

    RadioDriverManager::CarryLeDeviceIds();
    ReconcileVirtualMic();

    auto session = std::make_shared<RadioBridgeSession>(std::move(importedSocket), _radio, _hci);
    {
        std::lock_guard<std::mutex> lock{_sessionMutex};
        _radioSession = session;
    }

    // Periodic 500 ms maintenance tick while a live radio session is running: advances
    // deferred AAP opens (post-AVDTP / settle timeout), retries stalled L2CAP handshakes,
    // and resets watchdog failure counters once a session stays healthy for 60 s.
    std::atomic<bool> sessionActive{true};
    std::mutex tickMutex;
    std::condition_variable tickCv;
    std::thread tickThread{[&] {
        while (sessionActive.load()) {
            std::unique_lock<std::mutex> lk{tickMutex};
            tickCv.wait_for(
                lk, std::chrono::milliseconds(500), [&] { return !sessionActive.load(); });
            if (!sessionActive.load()) {
                break;
            }
            const uint64_t nowMs = SteadyNowMs();
            _hci.Tick(nowMs);
            _watchdog.MarkSessionHealthyIfElapsed(nowMs);
        }
    }};

    session->Run();

    sessionActive.store(false);
    tickCv.notify_all();
    if (tickThread.joinable()) {
        tickThread.join();
    }

    {
        std::lock_guard<std::mutex> lock{_sessionMutex};
        _radioSession.reset();
    }

    _hci.Reset("radio bridge session ended");
    UsbIpServer::DetachLocalPort(attachedPort);
    _usbIp.UnexportDevice(kRadioBusId);
    _radio.Close();

    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        _virtualRadioAttached = false;
        _virtualRadioUsbipPort = 0;
    }

    RadioDriverManager::ForgetHiddenVirtualAudioNodes(adapter.instanceId);

    if (_running.load() && !_suspended.load() && !_restoreRequested.load()) {
        const auto decision = _watchdog.RecordTakeoverFailure(SteadyNowMs());
        if (decision.action == Protocol::WatchdogPolicy::FailureAction::TripGuardAndRestoreNative) {
            RegistryStore::SetGuardTripped(true);
            ServiceLog::ReportEventLogError("AirpodsBridgeService tripped safety guard after "
                                            "session drops; restoring native Bluetooth driver.");
            RadioDriverManager::RestoreAllBoundAdapters();
            BroadcastStatus();
            return;
        }
        Sleep(decision.backoffMs);
    }
}

void RadioEngine::HandleUsbIpImport(const std::string &busId, UniqueSocket clientSocket)
{
    if (busId == kRadioBusId) {
        {
            std::lock_guard<std::mutex> lock{_sessionMutex};
            _pendingRadioSocket = std::move(clientSocket);
        }
        _radioImportCv.notify_one();
        return;
    }

    if (busId == kMicBusId) {
        std::lock_guard<std::mutex> lock{_micMutex};
        if (!_micEmulator) {
            return;
        }
        if (_micSession) {
            _micSession->Stop();
        }
        if (_micThread.joinable()) {
            _micThread.join();
        }
        _micSession = std::make_unique<MicBridgeSession>(std::move(clientSocket), *_micEmulator);
        _micAttached = true;
        auto *sessPtr = _micSession.get();
        _micThread = std::thread{[this, sessPtr] {
            sessPtr->Run();
            {
                std::lock_guard<std::mutex> mlock{_micMutex};
                _micAttached = false;
            }
            BroadcastStatus();
        }};
        BroadcastStatus();
    }
}

void RadioEngine::HandlePipeFrame(uint64_t clientId, const Protocol::Frame &frame)
{
    using Protocol::MessageType;

    switch (frame.type) {
    case MessageType::Hello:
    case MessageType::QueryStatus: {
        const auto snap = GetStatusSnapshot();
        _pipeServer.SendToClient(
            clientId, MessageType::StatusSnapshot, Protocol::EncodeStatusSnapshot(snap));
        break;
    }

    case MessageType::OpenAapChannel: {
        if (const auto addr = Protocol::DecodeAddressPayload(frame.payload); addr.has_value()) {
            _hci.OpenAapChannel(*addr);
        }
        break;
    }

    case MessageType::CloseAapChannel: {
        if (const auto addr = Protocol::DecodeAddressPayload(frame.payload); addr.has_value()) {
            _hci.CloseAapChannel(*addr);
        }
        break;
    }

    case MessageType::SendAapPacket: {
        if (const auto pkt = Protocol::DecodeAapPacket(frame.payload); pkt.has_value()) {
            _hci.SendAapPacket(pkt->bluetoothAddress, pkt->data);
        }
        break;
    }

    case MessageType::SetProxyConfig: {
        if (const auto cfg = Protocol::DecodeProxyConfig(frame.payload); cfg.has_value()) {
            _hci.SetConfig(*cfg);
            RegistryStore::SaveProxyConfig(*cfg);
            _pipeServer.SendToClient(
                clientId, MessageType::CommandAck,
                Protocol::EncodeCommandAck(Protocol::CommandAckMsg{
                    .requestType = MessageType::SetProxyConfig,
                    .ok = true,
                    .message = "Proxy configuration applied.",
                }));
            BroadcastStatus();
        }
        break;
    }

    case MessageType::ConfigureVirtualMic: {
        if (const auto cfg = Protocol::DecodeConfigureVirtualMic(frame.payload); cfg.has_value()) {
            {
                std::lock_guard<std::mutex> lock{_micMutex};
                _micWanted = cfg->enable;
                _micAddress = cfg->bluetoothAddress;
                _micHdVoice = cfg->hdVoice;
                _micName = cfg->deviceName;
            }
            ReconcileVirtualMic();
            BroadcastStatus();
        }
        break;
    }

    case MessageType::PushMicPcm: {
        if (const auto pcm = Protocol::DecodeMicPcm(frame.payload); pcm.has_value()) {
            std::lock_guard<std::mutex> lock{_micMutex};
            if (_micEmulator && pcm->pcmBytes.size() >= 2) {
                const auto *samples = reinterpret_cast<const int16_t *>(pcm->pcmBytes.data());
                const size_t count = pcm->pcmBytes.size() / sizeof(int16_t);
                _micEmulator->PushPcm(
                    pcm->samplePosition, std::span<const int16_t>{samples, count});
            }
        }
        break;
    }

    case MessageType::RetryAfterGuard: {
        _restoreRequested.store(false);
        _watchdog.ClearGuard();
        RegistryStore::SetGuardTripped(false);
        _supervisorCv.notify_all();
        _pipeServer.SendToClient(
            clientId, MessageType::CommandAck,
            Protocol::EncodeCommandAck(Protocol::CommandAckMsg{
                .requestType = MessageType::RetryAfterGuard,
                .ok = true,
                .message = "Guard cleared; retrying radio takeover.",
            }));
        BroadcastStatus();
        break;
    }

    case MessageType::RestoreNativeDriver: {
        _restoreRequested.store(true);
        std::shared_ptr<RadioBridgeSession> sess;
        {
            std::lock_guard<std::mutex> lock{_sessionMutex};
            sess = _radioSession;
        }
        if (sess) {
            sess->Stop();
        }
        _supervisorCv.notify_all();
        _pipeServer.SendToClient(
            clientId, MessageType::CommandAck,
            Protocol::EncodeCommandAck(Protocol::CommandAckMsg{
                .requestType = MessageType::RestoreNativeDriver,
                .ok = true,
                .message = "Restoring native Bluetooth driver.",
            }));
        BroadcastStatus();
        break;
    }

    default:
        break;
    }
}

void RadioEngine::BroadcastStatus()
{
    const auto snap = GetStatusSnapshot();
    _pipeServer.Broadcast(
        Protocol::MessageType::StatusSnapshot, Protocol::EncodeStatusSnapshot(snap));
}

void RadioEngine::SetState(Protocol::RadioServiceState state, std::string detail)
{
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        _state = state;
        _detail = std::move(detail);
    }
    ServiceLog::Write(
        LogLevel::Info, "engine",
        std::string{"State -> "} + Protocol::ToString(state) + ": " + _detail);
    BroadcastStatus();
}

void RadioEngine::ReconcileVirtualMic()
{
    std::unique_ptr<MicBridgeSession> oldSess;
    int oldPort = 0;
    bool needAttach = false;
    Protocol::UsbIp::ExportedDevice exportDev{};

    {
        std::lock_guard<std::mutex> lock{_micMutex};
        const bool sameDevice =
            _micEmulator && _micEmulator->BluetoothAddress() == _micAddress && _micAttached;
        if (!_micWanted || _micAddress == 0 || !sameDevice) {
            DetachVirtualMicLocked(oldSess, oldPort);
        }
        if (_micWanted && _micAddress != 0 && !_micEmulator) {
            _micEmulator =
                std::make_unique<Protocol::Uac1MicEmulator>(_micAddress, _micName, _micHdVoice);
            const uint64_t addr = _micAddress;
            _micEmulator->SetOnCaptureChanged([this, addr](bool capturing) {
                const auto payload = Protocol::EncodeMicCaptureState(Protocol::MicCaptureStateMsg{
                    .bluetoothAddress = addr,
                    .capturing = capturing,
                });
                _pipeServer.Broadcast(Protocol::MessageType::MicCaptureState, payload);
                BroadcastStatus();
            });
            exportDev = _micEmulator->MakeExportedDevice(kMicBusId, 2);
            needAttach = true;
        }
    }

    if (oldSess) {
        oldSess->Stop();
    }
    if (_micThread.joinable() && oldSess) {
        _micThread.join();
    }
    if (oldPort > 0) {
        UsbIpServer::DetachLocalPort(oldPort);
        _usbIp.UnexportDevice(kMicBusId);
    }

    if (needAttach) {
        _usbIp.ExportDevice(exportDev);
        if (const auto portOpt = _usbIp.AttachLocalDevice(kMicBusId); portOpt.has_value()) {
            std::lock_guard<std::mutex> lock{_micMutex};
            _micUsbipPort = *portOpt;
        }
        else {
            _usbIp.UnexportDevice(kMicBusId);
            std::lock_guard<std::mutex> lock{_micMutex};
            _micEmulator.reset();
        }
    }
}

void RadioEngine::DetachVirtualMicLocked(
    std::unique_ptr<MicBridgeSession> &sessionOut, int &portOut)
{
    sessionOut = std::move(_micSession);
    portOut = _micUsbipPort;
    _micUsbipPort = 0;
    _micAttached = false;
    _micEmulator.reset();
}

} // namespace Service::Win
