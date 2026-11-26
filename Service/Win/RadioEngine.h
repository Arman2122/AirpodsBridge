// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "Service/Protocol/HciProxyEngine.h"
#include "Service/Protocol/ServiceProtocol.h"
#include "Service/Protocol/Uac1MicEmulator.h"
#include "Service/Protocol/WatchdogPolicy.h"
#include "Service/Win/MicBridgeSession.h"
#include "Service/Win/NamedPipeServer.h"
#include "Service/Win/RadioBridgeSession.h"
#include "Service/Win/UsbIpServer.h"
#include "Service/Win/WinUsbRadio.h"

namespace Service::Win {

//
// Top-level supervisor for the AirpodsBridge Radio Service.
//
// Coordinates:
//   - Pre-flight check of Microsoft-signed `usbip-win2` (`usbip2_ude`)
//   - Fail-safe `WatchdogPolicy` (3 failures in 10 min -> automatic native driver restore)
//   - WinUSB takeover + USB/IP virtual Bluetooth adapter + HCI/L2CAP MITM proxy
//   - Optional virtual UAC1 stereo-safe microphone over USB/IP (`usbaudio.sys`)
//   - Local Named Pipe IPC for `AirpodsBridge.exe`
//   - Windows power suspend/resume lifecycle
//
class RadioEngine
{
public:
    RadioEngine();
    ~RadioEngine();

    RadioEngine(const RadioEngine &) = delete;
    RadioEngine &operator=(const RadioEngine &) = delete;

    void Start();
    void Stop(bool systemShutdown = false);

    void OnSystemSuspend();
    void OnSystemResume();

    Protocol::StatusSnapshot GetStatusSnapshot() const;

private:
    void SupervisorLoop();
    void RunTakeoverCycle();
    void WaitInterruptible(uint32_t ms);
    void HandleUsbIpImport(const std::string &busId, UniqueSocket clientSocket);
    void HandlePipeFrame(uint64_t clientId, const Protocol::Frame &frame);
    void BroadcastStatus();
    void SetState(Protocol::RadioServiceState state, std::string detail);
    void ReconcileVirtualMic();
    void DetachVirtualMicLocked(std::unique_ptr<MicBridgeSession> &sessionOut, int &portOut);

    std::atomic<bool> _running{false};
    std::atomic<bool> _suspended{false};
    std::atomic<bool> _restoreRequested{false};
    std::atomic<bool> _systemShutdown{false};

    mutable std::mutex _stateMutex;
    std::condition_variable _supervisorCv;
    Protocol::RadioServiceState _state{Protocol::RadioServiceState::Stopped};
    std::string _detail;
    std::wstring _activeAdapterInstance;
    std::string _activeAdapterDisplayName;
    uint16_t _activeAdapterVid{0};
    uint16_t _activeAdapterPid{0};
    bool _usbipReady{false};
    bool _winUsbBound{false};
    bool _virtualRadioAttached{false};
    int _virtualRadioUsbipPort{0};

    Protocol::WatchdogPolicy _watchdog;
    Protocol::HciProxyEngine _hci;
    WinUsbRadio _radio;
    UsbIpServer _usbIp;
    NamedPipeServer _pipeServer;

    std::mutex _sessionMutex;
    std::shared_ptr<RadioBridgeSession> _radioSession;
    std::condition_variable _radioImportCv;
    UniqueSocket _pendingRadioSocket;

    // Optional virtual UAC1 microphone state.
    mutable std::mutex _micMutex;
    bool _micWanted{false};
    uint64_t _micAddress{0};
    bool _micHdVoice{false};
    std::string _micName;
    std::unique_ptr<Protocol::Uac1MicEmulator> _micEmulator;
    std::unique_ptr<MicBridgeSession> _micSession;
    std::thread _micThread;
    int _micUsbipPort{0};
    bool _micAttached{false};

    std::thread _supervisorThread;
};

} // namespace Service::Win
