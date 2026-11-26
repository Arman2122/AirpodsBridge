// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/AirPodsServiceTransport_win.h"

#include <shellapi.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

#include <nlohmann/json.hpp>

#include "Service/Protocol/ServiceProtocol.h"
#include "Source/Support/Logger.h"

namespace Core::AirPods {

namespace {

constexpr wchar_t kRadioServiceName[] = L"AirpodsBridgeService";

std::pair<bool, bool> QueryScmServiceState()
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (scm == nullptr) {
        return {false, false};
    }
    SC_HANDLE svc = OpenServiceW(scm, kRadioServiceName, SERVICE_QUERY_STATUS);
    if (svc == nullptr) {
        CloseServiceHandle(scm);
        return {false, false};
    }
    SERVICE_STATUS status{};
    const bool running =
        QueryServiceStatus(svc, &status) && status.dwCurrentState == SERVICE_RUNNING;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return {true, running};
}

std::filesystem::path FindServiceBinaryPath()
{
    wchar_t exePath[MAX_PATH]{};
    std::error_code ec;
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        std::filesystem::path dir = std::filesystem::path{exePath}.parent_path();
        for (int depth = 0; depth < 5 && !dir.empty(); ++depth) {
            const auto direct = dir / L"AirpodsBridgeService.exe";
            if (std::filesystem::is_regular_file(direct, ec)) {
                return direct;
            }
            const auto built =
                dir / L"Build" / L"service-x64" / L"Binary" / L"AirpodsBridgeService.exe";
            if (std::filesystem::is_regular_file(built, ec)) {
                return built;
            }
            dir = dir.parent_path();
        }
    }
    wchar_t pf[MAX_PATH]{};
    if (ExpandEnvironmentStringsW(L"%ProgramFiles%", pf, MAX_PATH) > 0) {
        const auto installed = std::filesystem::path{pf} / L"AirpodsBridge" / L"RadioService" /
                               L"AirpodsBridgeService.exe";
        if (std::filesystem::is_regular_file(installed, ec)) {
            return installed;
        }
    }
    return {};
}

struct ParsedProgress {
    std::string status;
    int percent{0};
    std::string message;
};

std::optional<ParsedProgress> ReadSetupProgressFile(const wchar_t *path)
{
    std::ifstream f{path, std::ios::in | std::ios::binary};
    if (!f.is_open()) {
        return std::nullopt;
    }
    try {
        const auto j = nlohmann::json::parse(f);
        ParsedProgress p{};
        p.status = j.value("status", std::string{});
        p.percent = j.value("percent", 0);
        p.message = j.value("message", std::string{});
        return p;
    }
    catch (...) {
        return std::nullopt;
    }
}

} // namespace

ServiceControlTransport::ServiceControlTransport()
{
    const auto [installed, running] = QueryScmServiceState();
    _lastInfo.serviceInstalled = installed;
    _lastInfo.serviceRunning = running;
}

ServiceControlTransport::~ServiceControlTransport()
{
    Disconnect();
    std::thread toJoin;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        ClosePipeLocked(toJoin);
    }
    if (toJoin.joinable()) {
        toJoin.join();
    }
}

bool ServiceControlTransport::IsServiceInstalled()
{
    return QueryScmServiceState().first;
}

bool ServiceControlTransport::IsServiceRunning()
{
    return QueryScmServiceState().second;
}

void ServiceControlTransport::RunElevatedSetupAsync(
    bool uninstall, bool restoreOnly, SetupCallback onComplete, SetupProgressCallback onProgress)
{
    const auto serviceExe = FindServiceBinaryPath();
    if (serviceExe.empty()) {
        LOG(Warn, "ServiceControlTransport: AirpodsBridgeService.exe (x64) not found.");
        if (onComplete) {
            onComplete(SetupOutcome{
                .succeeded = false,
                .message = "AirpodsBridgeService.exe was not found next to AirpodsBridge.exe.",
            });
        }
        return;
    }

    const std::wstring arg =
        restoreOnly ? L"--setup-restore" : (uninstall ? L"--setup-disable" : L"--setup-enable");
    const std::wstring workDir = serviceExe.parent_path().wstring();

    std::thread([exe = serviceExe.wstring(), arg, workDir, onComplete = std::move(onComplete),
                 onProgress = std::move(onProgress)]() mutable {
        constexpr wchar_t kProgressPath[] = L"C:\\ProgramData\\AirpodsBridge\\SetupProgress.json";
        DeleteFileW(kProgressPath);

        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS;
        sei.lpVerb = L"runas";
        sei.lpFile = exe.c_str();
        sei.lpParameters = arg.c_str();
        sei.lpDirectory = workDir.c_str();
        // Completely silent: zero PowerShell or console window shown to the user.
        sei.nShow = SW_HIDE;

        SetupOutcome outcome{};
        if (!ShellExecuteExW(&sei)) {
            const DWORD err = GetLastError();
            outcome.cancelled = (err == ERROR_CANCELLED);
            outcome.message =
                outcome.cancelled
                    ? "Administrator prompt was cancelled."
                    : ("Failed to launch setup helper (Win32 error " + std::to_string(err) + ").");
            if (onComplete) {
                onComplete(std::move(outcome));
            }
            return;
        }

        DWORD exitCode = 1;
        int lastPct = -1;
        std::string lastMsg;
        if (sei.hProcess != nullptr) {
            while (WaitForSingleObject(sei.hProcess, 150) == WAIT_TIMEOUT) {
                if (const auto prog = ReadSetupProgressFile(kProgressPath); prog.has_value()) {
                    if (prog->percent != lastPct || prog->message != lastMsg) {
                        lastPct = prog->percent;
                        lastMsg = prog->message;
                        if (onProgress) {
                            onProgress(prog->percent, prog->message);
                        }
                    }
                }
            }
            GetExitCodeProcess(sei.hProcess, &exitCode);
            CloseHandle(sei.hProcess);
        }

        const auto finalProg = ReadSetupProgressFile(kProgressPath);
        outcome.rebootRequired =
            (exitCode == 3010 || (finalProg.has_value() && finalProg->status == "reboot_required"));
        outcome.succeeded = (exitCode == 0 || outcome.rebootRequired);

        if (finalProg.has_value() && !finalProg->message.empty()) {
            outcome.message = finalProg->message;
        }
        else if (outcome.succeeded) {
            outcome.message = "Control Engine updated.";
        }
        else {
            outcome.message = "Setup exited with code " + std::to_string(exitCode) +
                              ". See C:\\ProgramData\\AirpodsBridge\\RadioService.log.";
        }

        if (onComplete) {
            onComplete(std::move(outcome));
        }
    }).detach();
}

bool ServiceControlTransport::IsAvailable() const
{
    return IsServiceRunning();
}

bool ServiceControlTransport::IsConnected() const
{
    return _aapConnected.load();
}

void ServiceControlTransport::Connect(uint64_t bluetoothAddress)
{
    if (bluetoothAddress == 0) {
        Disconnect();
        return;
    }

    {
        std::lock_guard<std::mutex> lock{_mutex};
        _targetAddress = bluetoothAddress;
    }

    if (!EnsurePipeConnected()) {
        return;
    }

    // Push latest proxy options and request hidden L2CAP AAP channel on PSM 0x1001.
    ApplyRadioProxyOptions(_options);
    const auto payload = Service::Protocol::EncodeAddressPayload(bluetoothAddress);
    SendRawFrame(static_cast<uint16_t>(Service::Protocol::MessageType::OpenAapChannel), payload);
}

void ServiceControlTransport::Disconnect()
{
    uint64_t prevAddr = 0;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        prevAddr = _targetAddress;
        _targetAddress = 0;
    }

    if (prevAddr != 0 && _pipeConnected.load()) {
        const auto payload = Service::Protocol::EncodeAddressPayload(prevAddr);
        SendRawFrame(
            static_cast<uint16_t>(Service::Protocol::MessageType::CloseAapChannel), payload);
    }

    if (_aapConnected.exchange(false)) {
        NotifyConnectionChanged(false);
    }
}

bool ServiceControlTransport::SendPacket(std::span<const uint8_t> packet)
{
    if (packet.empty() || packet.size() > Service::Protocol::kMaxAapPacketBytes ||
        !_aapConnected.load())
    {
        return false;
    }

    uint64_t addr = 0;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        addr = _targetAddress;
    }
    if (addr == 0) {
        return false;
    }

    const auto payload = Service::Protocol::EncodeAapPacket(Service::Protocol::AapPacketMsg{
        .bluetoothAddress = addr,
        .data = std::vector<uint8_t>{packet.begin(), packet.end()},
    });
    return SendRawFrame(
        static_cast<uint16_t>(Service::Protocol::MessageType::SendAapPacket), payload);
}

void ServiceControlTransport::SetLowLatencyMode(bool enabled)
{
    RadioProxyOptions next;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _options.lowLatencyQos = enabled;
        next = _options;
    }
    if (_pipeConnected.load()) {
        ApplyRadioProxyOptions(next);
    }
}

std::optional<RadioServiceInfo> ServiceControlTransport::GetRadioServiceInfo() const
{
    const auto [installed, running] = QueryScmServiceState();
    auto *mutableThis = const_cast<ServiceControlTransport *>(this);
    if (running && !_pipeConnected.load()) {
        mutableThis->EnsurePipeConnected();
    }

    std::lock_guard<std::mutex> lock{_mutex};
    RadioServiceInfo info = _lastInfo;
    info.serviceInstalled = installed;
    info.serviceRunning = running;
    info.pipeConnected = _pipeConnected.load();
    if (!installed) {
        info.stateName = "not_installed";
        info.detail = "Radio service is not installed (BLE Broadcast mode active).";
    }
    else if (!running) {
        info.stateName = "stopped";
        info.detail = "AirpodsBridgeService is installed but not running.";
    }
    return info;
}

bool ServiceControlTransport::ApplyRadioProxyOptions(const RadioProxyOptions &options)
{
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _options = options;
    }
    if (!EnsurePipeConnected()) {
        return false;
    }
    const auto payload = Service::Protocol::EncodeProxyConfig(Service::Protocol::ProxyConfig{
        .lowLatencyQos = options.lowLatencyQos,
        .pollLatencyUs = options.pollLatencyUs,
        .declineHfp = options.declineHfp,
        .virtualMicEnabled = options.virtualMicEnabled,
    });
    return SendRawFrame(
        static_cast<uint16_t>(Service::Protocol::MessageType::SetProxyConfig), payload);
}

bool ServiceControlTransport::ConfigureVirtualMic(
    uint64_t bluetoothAddress, bool enable, bool hdVoice, const std::string &deviceName)
{
    if (!EnsurePipeConnected()) {
        return false;
    }
    const auto payload =
        Service::Protocol::EncodeConfigureVirtualMic(Service::Protocol::ConfigureVirtualMicMsg{
            .bluetoothAddress = bluetoothAddress,
            .enable = enable,
            .hdVoice = hdVoice,
            .deviceName = deviceName,
        });
    return SendRawFrame(
        static_cast<uint16_t>(Service::Protocol::MessageType::ConfigureVirtualMic), payload);
}

bool ServiceControlTransport::RetryRadioServiceAfterGuard()
{
    if (!EnsurePipeConnected()) {
        return false;
    }
    return SendRawFrame(static_cast<uint16_t>(Service::Protocol::MessageType::RetryAfterGuard), {});
}

bool ServiceControlTransport::RestoreNativeRadioDriver()
{
    if (!EnsurePipeConnected()) {
        return false;
    }
    return SendRawFrame(
        static_cast<uint16_t>(Service::Protocol::MessageType::RestoreNativeDriver), {});
}

void ServiceControlTransport::SetOnPacketReceived(PacketCallback callback)
{
    std::lock_guard<std::mutex> lock{_callbackMutex};
    _onPacketReceived = std::move(callback);
}

void ServiceControlTransport::SetOnConnectionChanged(ConnectionCallback callback)
{
    std::lock_guard<std::mutex> lock{_callbackMutex};
    _onConnectionChanged = std::move(callback);
}

bool ServiceControlTransport::EnsurePipeConnected()
{
    if (_pipeConnected.load()) {
        return true;
    }
    if (!IsServiceRunning()) {
        return false;
    }

    HANDLE pipe = CreateFileW(
        Service::Protocol::kPipeNameW, GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return false;
    }

    HANDLE stopEv = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (stopEv == nullptr) {
        CloseHandle(pipe);
        return false;
    }

    std::thread oldThread;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        ClosePipeLocked(oldThread);
        _pipeHandle = pipe;
        _stopEvent = stopEv;
        _pipeConnected.store(true);
        _readerThread = std::thread{&ServiceControlTransport::ReaderLoop, this, pipe, stopEv};
    }
    if (oldThread.joinable() && oldThread.get_id() != std::this_thread::get_id()) {
        oldThread.join();
    }

    SendRawFrame(static_cast<uint16_t>(Service::Protocol::MessageType::Hello), {});
    LOG(Info, "ServiceControlTransport: connected to AirpodsBridgeService Named Pipe.");
    return true;
}

void ServiceControlTransport::ClosePipeLocked(std::thread &readerToJoin)
{
    _pipeConnected.store(false);
    if (_stopEvent != nullptr) {
        SetEvent(_stopEvent);
    }
    if (_pipeHandle != INVALID_HANDLE_VALUE) {
        CancelIoEx(_pipeHandle, nullptr);
        CloseHandle(_pipeHandle);
        _pipeHandle = INVALID_HANDLE_VALUE;
    }
    readerToJoin = std::move(_readerThread);
    if (_stopEvent != nullptr && !readerToJoin.joinable()) {
        CloseHandle(_stopEvent);
        _stopEvent = nullptr;
    }
}

bool ServiceControlTransport::SendRawFrame(uint16_t messageType, std::span<const uint8_t> payload)
{
    HANDLE pipe = INVALID_HANDLE_VALUE;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        pipe = _pipeHandle;
    }
    if (pipe == INVALID_HANDLE_VALUE || !_pipeConnected.load()) {
        return false;
    }

    const auto encoded = Service::Protocol::EncodeFrame(
        static_cast<Service::Protocol::MessageType>(messageType), payload);

    std::lock_guard<std::mutex> wlock{_writeMutex};
    HANDLE ev = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (ev == nullptr) {
        return false;
    }
    OVERLAPPED ov{};
    ov.hEvent = ev;
    DWORD written = 0;
    BOOL ok = WriteFile(pipe, encoded.data(), static_cast<DWORD>(encoded.size()), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ev, 2000) == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(pipe, &ov, &written, FALSE);
        }
        else {
            CancelIoEx(pipe, &ov);
            ok = FALSE;
        }
    }
    CloseHandle(ev);
    return ok != FALSE && written == encoded.size();
}

void ServiceControlTransport::ReaderLoop(HANDLE pipeHandle, HANDLE stopEvent)
{
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (readEvent == nullptr) {
        CloseHandle(stopEvent);
        return;
    }

    std::array<uint8_t, 8192> chunk{};
    std::vector<uint8_t> stream;
    stream.reserve(16384);

    while (WaitForSingleObject(stopEvent, 0) != WAIT_OBJECT_0) {
        ResetEvent(readEvent);
        OVERLAPPED ov{};
        ov.hEvent = readEvent;

        DWORD got = 0;
        BOOL ok = ReadFile(pipeHandle, chunk.data(), static_cast<DWORD>(chunk.size()), &got, &ov);
        if (!ok) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {stopEvent, readEvent};
                const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(pipeHandle, &ov);
                    break;
                }
                ok = GetOverlappedResult(pipeHandle, &ov, &got, FALSE);
            }
            if (!ok) {
                break;
            }
        }
        if (got == 0) {
            break;
        }

        stream.insert(stream.end(), chunk.begin(), chunk.begin() + got);
        for (;;) {
            Service::Protocol::Frame frame{};
            size_t consumed = 0;
            const auto st = Service::Protocol::TryDecodeFrame(stream, frame, consumed);
            if (st == Service::Protocol::DecodeStatus::NeedMoreData) {
                break;
            }
            if (st == Service::Protocol::DecodeStatus::InvalidFrame) {
                stream.clear();
                break;
            }
            stream.erase(stream.begin(), stream.begin() + static_cast<std::ptrdiff_t>(consumed));
            HandleInboundFrame(static_cast<uint16_t>(frame.type), frame.payload);
        }
    }

    CloseHandle(readEvent);
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (_stopEvent == stopEvent) {
            _stopEvent = nullptr;
        }
    }
    CloseHandle(stopEvent);
    _pipeConnected.store(false);
    if (_aapConnected.exchange(false)) {
        NotifyConnectionChanged(false);
    }
}

void ServiceControlTransport::HandleInboundFrame(uint16_t rawType, std::span<const uint8_t> payload)
{
    using Service::Protocol::MessageType;
    const auto type = static_cast<MessageType>(rawType);

    if (type == MessageType::StatusSnapshot) {
        if (const auto snap = Service::Protocol::DecodeStatusSnapshot(payload); snap.has_value()) {
            std::lock_guard<std::mutex> lock{_mutex};
            _lastInfo.serviceInstalled = true;
            _lastInfo.serviceRunning = true;
            _lastInfo.pipeConnected = true;
            _lastInfo.usbipDriverReady = snap->usbipDriverReady;
            _lastInfo.winUsbBound = snap->winUsbBound;
            _lastInfo.virtualAdapterAttached = snap->virtualAdapterAttached;
            _lastInfo.guardTripped = snap->guardTripped;
            _lastInfo.callAudioActive = snap->callAudioActive;
            _lastInfo.virtualMicAttached = snap->virtualMicAttached;
            _lastInfo.virtualMicCapturing = snap->virtualMicCapturing;
            _lastInfo.lowLatencyQosEnabled = snap->lowLatencyQosEnabled;
            _lastInfo.declineHfpEnabled = snap->declineHfpEnabled;
            _lastInfo.adapterVid = snap->adapterVid;
            _lastInfo.adapterPid = snap->adapterPid;
            _lastInfo.hciManufacturer = snap->hciManufacturer;
            _lastInfo.activeAapChannels = snap->activeAapChannels;
            _lastInfo.activePollLatencyUs = snap->activePollLatencyUs;
            _lastInfo.recentFailures = snap->recentFailures;
            _lastInfo.injectedPackets = snap->injectedPackets;
            _lastInfo.interceptedPackets = snap->interceptedPackets;
            _lastInfo.stateName = Service::Protocol::ToString(snap->state);
            _lastInfo.adapterDisplayName = snap->adapterDisplayName;
            _lastInfo.adapterInstanceId = snap->adapterInstanceId;
            _lastInfo.detail = snap->detail;
        }
        return;
    }

    if (type == MessageType::AapChannelState) {
        if (const auto msg = Service::Protocol::DecodeAapChannelState(payload); msg.has_value()) {
            uint64_t wanted = 0;
            {
                std::lock_guard<std::mutex> lock{_mutex};
                wanted = _targetAddress;
            }
            if (wanted == 0 || wanted == msg->bluetoothAddress) {
                const bool changed = _aapConnected.exchange(msg->connected) != msg->connected;
                if (changed) {
                    NotifyConnectionChanged(msg->connected);
                }
            }
        }
        return;
    }

    if (type == MessageType::AapPacketReceived) {
        if (const auto msg = Service::Protocol::DecodeAapPacket(payload); msg.has_value()) {
            PacketCallback cb;
            {
                std::lock_guard<std::mutex> lock{_callbackMutex};
                cb = _onPacketReceived;
            }
            if (cb) {
                cb(msg->data);
            }
        }
    }
}

void ServiceControlTransport::NotifyConnectionChanged(bool connected)
{
    ConnectionCallback cb;
    {
        std::lock_guard<std::mutex> lock{_callbackMutex};
        cb = _onConnectionChanged;
    }
    if (cb) {
        cb(connected);
    }
}

} // namespace Core::AirPods
