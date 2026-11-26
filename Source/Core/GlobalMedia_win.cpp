// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/GlobalMedia_win.h"

#include <Functiondiscoverykeys_devpkey.h>

#include "Source/Support/Utils.h"
#include "Source/Support/Logger.h"

namespace Core::GlobalMedia {

using namespace std::chrono_literals;
using namespace winrt::Windows::Media::Control;

namespace Details {

class MediaProgramThroughVirtualKeyAbstract : public MediaProgramAbstract
{
public:
    enum WindowMatchingFlag : uint32_t {
        HasChildren = 0b00000001,
        NonEmptyTitle = 0b00000010,
    };
    Q_DECLARE_FLAGS(WindowMatchingFlags, WindowMatchingFlag)

    virtual inline ~MediaProgramThroughVirtualKeyAbstract() {}

    bool IsAvailable() override
    {
        if (_windowProcess.has_value()) {
            return true;
        }

        _windowProcess = FindWindowAndProcess();
        if (!_windowProcess.has_value()) {
            return false;
        }

        _audioMeterInfo = GetProcessAudioMeterInfo();
        return true;
    }

    bool IsPlaying() const override
    {
        auto optAudioVolume = GetProcessAudioVolume();
        return optAudioVolume.has_value() && optAudioVolume.value() != 0.f;
    }

    bool Play() override
    {
        LOG(Trace, "Do play.");

        if (IsPlaying()) {
            LOG(Trace, "The media program is already playing.");
            return true;
        }
        return Switch();
    }

    bool Pause() override
    {
        LOG(Trace, "Do pause.");

        if (!IsPlaying()) {
            LOG(Trace, "The media program nothing is playing.");
            return true;
        }
        return Switch();
    }

protected:
    virtual std::wstring GetProcessName() const = 0;
    virtual std::wstring GetWindowClassName() const = 0;
    virtual std::optional<std::wstring> GetWindowTitleName() const
    {
        return std::nullopt;
    }
    virtual WindowMatchingFlags GetWindowMatchingFlags() const = 0;

private:
    std::optional<std::pair<HWND, uint32_t>> _windowProcess;
    Support::OS::Windows::Com::UniquePtr<IAudioMeterInformation> _audioMeterInfo;

    Support::OS::Windows::Com::UniquePtr<IAudioMeterInformation> GetProcessAudioMeterInfo()
    {
        LOG(Trace, "Try to get IAudioMeterInformation of this process.");

        Support::OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> deviceEnumerator;
        HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, deviceEnumerator.GetIID(),
            (void **)deviceEnumerator.ReleaseAndAddressOf());
        if (FAILED(result)) {
            LOG(Trace, "Create COM instance 'IMMDeviceEnumerator' failed. HRESULT: {:#x}", result);
            return {};
        }

        Support::OS::Windows::Com::UniquePtr<IMMDevice> audioEndpoint;
        result = deviceEnumerator->GetDefaultAudioEndpoint(
            eRender, eMultimedia, audioEndpoint.ReleaseAndAddressOf());
        if (FAILED(result)) {
            LOG(Trace, "'GetDefaultAudioEndpoint' failed. HRESULT: {:#x}", result);
            return {};
        }

        Support::OS::Windows::Com::UniquePtr<IAudioSessionManager2> sessionMgr;
        result = audioEndpoint->Activate(
            sessionMgr.GetIID(), CLSCTX_ALL, nullptr, (void **)sessionMgr.ReleaseAndAddressOf());
        if (FAILED(result)) {
            LOG(Trace, "'IMMDevice::Activate' IAudioSessionManager2 failed. HRESULT: {:#x}",
                result);
            return {};
        }

        Support::OS::Windows::Com::UniquePtr<IAudioSessionEnumerator> sessionEnumerator;
        result = sessionMgr->GetSessionEnumerator(sessionEnumerator.ReleaseAndAddressOf());
        if (FAILED(result)) {
            LOG(Trace, "'IAudioSessionManager2::GetSessionEnumerator' failed. HRESULT: {:#x}",
                result);
            return {};
        }

        int sessionCount = 0;
        result = sessionEnumerator->GetCount(&sessionCount);
        if (FAILED(result)) {
            LOG(Trace, "'IAudioSessionEnumerator::GetCount' failed. HRESULT: {:#x}", result);
            return {};
        }

        for (int i = 0; i < sessionCount; ++i) {
            LOG(Trace, "Enumerating the {}th session.", i);

            Support::OS::Windows::Com::UniquePtr<IAudioSessionControl> session;
            result = sessionEnumerator->GetSession(i, session.ReleaseAndAddressOf());
            if (FAILED(result)) {
                LOG(Trace, "'IAudioSessionEnumerator::GetSession' failed. HRESULT: {:#x}", result);
                continue;
            }

            Support::OS::Windows::Com::UniquePtr<IAudioSessionControl2> sessionEx;
            result = session->QueryInterface(
                sessionEx.GetIID(), (void **)sessionEx.ReleaseAndAddressOf());
            if (FAILED(result)) {
                LOG(Trace,
                    "'IAudioSessionControl::QueryInterface' IAudioSessionControl2 failed. "
                    "HRESULT: {:#x}",
                    result);
                continue;
            }

            uint32_t thisProcessId = 0;
            result = sessionEx->GetProcessId((DWORD *)&thisProcessId);
            if (FAILED(result)) {
                LOG(Trace, "'IAudioSessionControl2::GetProcessId' failed. HRESULT: {:#x}", result);
                continue;
            }

            LOG(Trace, "Get the session process id: {}", thisProcessId);

            if (_windowProcess->second != thisProcessId) {
                LOG(Trace, "Process id mismatch, try next.");
                continue;
            }

            Support::OS::Windows::Com::UniquePtr<IAudioMeterInformation> audioMeterInfo;
            result = sessionEx->QueryInterface(
                audioMeterInfo.GetIID(), (void **)audioMeterInfo.ReleaseAndAddressOf());
            if (FAILED(result)) {
                LOG(Trace,
                    "'IAudioSessionControl2::QueryInterface' IAudioMeterInformation "
                    "failed. HRESULT: {:#x}",
                    result);
                return {};
            }

            LOG(Trace, "Get IAudioMeterInformation successfully.");
            return audioMeterInfo;
        }

        LOG(Trace, "The desired IAudioMeterInformation not found.");
        return {};
    }

    std::optional<float> GetProcessAudioVolume() const
    {
        if (!_audioMeterInfo) {
            return std::nullopt;
        }

        float peak = 0.f;
        HRESULT result = _audioMeterInfo->GetPeakValue(&peak);
        if (FAILED(result)) {
            LOG(Trace, "'IAudioMeterInformation::GetPeakValue' failed. HRESULT: {:#x}", result);
            return std::nullopt;
        }

        return peak;
    }

    std::optional<std::pair<HWND, uint32_t>> FindWindowAndProcess() const
    {
        auto processName = GetProcessName();
        auto className = GetWindowClassName();
        auto optTitleName = GetWindowTitleName();

        LOG(Trace, L"Try to find the media window. Process name: '{}', Class name: '{}'",
            processName, className);

        auto windowsInfo = Support::OS::Windows::Window::FindWindowsInfo(className, optTitleName);
        if (windowsInfo.empty()) {
            LOG(Trace, L"No matching media windows found.");
            return std::nullopt;
        }

        WindowMatchingFlags windowMatchingFlags = GetWindowMatchingFlags();

        for (const auto &info : windowsInfo) {
            // LOG(Trace,
            //     L"Enumerating window. hwnd: {}, Process id: {}, Process name: {}",
            //     (void*)info.hwnd,
            //     info.processId,
            //     info.processName
            // );

            if (Utils::Text::ToLower(info.processName) != Utils::Text::ToLower(processName)) {
                // LOG(Trace, L"The media window process name mismatch.");
                continue;
            }

            if (windowMatchingFlags.testFlag(WindowMatchingFlag::HasChildren)) {
                if (GetWindow(info.hwnd, GW_CHILD) == 0) {
                    // LOG(Trace, L"The window doesn't have any child windows.");
                    continue;
                }
            }

            if (windowMatchingFlags.testFlag(WindowMatchingFlag::NonEmptyTitle)) {
                if (info.titleName.empty()) {
                    // LOG(Trace, L"The window doesn't have title name.");
                    continue;
                }
            }

            LOG(Trace,
                L"The media window is matched. Return the information. hwnd: {}, Process "
                L"id: {}, Process name: {}",
                (void *)info.hwnd, info.processId, info.processName);

            return std::make_pair(info.hwnd, info.processId);
        }

        LOG(Trace, L"The desired media window not found.");
        return std::nullopt;
    }

    bool Switch()
    {
        bool postDown = PostMessageW(_windowProcess->first, WM_KEYDOWN, VK_SPACE, 0) != 0;

        std::this_thread::sleep_for(50ms);

        bool postUp = PostMessageW(_windowProcess->first, WM_KEYUP, VK_SPACE, 0) != 0;

        if (!postDown || !postUp) {
            LOG(Trace, "Switch failed. Post messages failed: hwnd '{}' down '{}' up '{}'",
                (void *)_windowProcess->first, postDown, postUp);
            return false;
        }
        return true;
    }
};

Q_DECLARE_OPERATORS_FOR_FLAGS(MediaProgramThroughVirtualKeyAbstract::WindowMatchingFlags)

//////////////////////////////////////////////////
// Media programs
//

class UniversalSystemSession final : public MediaProgramAbstract
{
public:
    explicit UniversalSystemSession(GlobalSystemMediaTransportControlsSession session)
        : _session{std::move(session)}
    {
    }

    bool IsAvailable() override
    {
        return static_cast<bool>(_session);
    }

    bool IsPlaying() const override
    {
        return IsPlaying(_session);
    }

    bool Play() override
    {
        try {
            _session.TryPlayAsync().get();
            return true;
        }
        catch (const Support::OS::Windows::Winrt::Exception &ex) {
            LOG(Warn, "_session.TryPlayAsync() failed. {}", Helper::ToString(ex));
            return false;
        }
    }

    bool Pause() override
    {
        try {
            _session.TryPauseAsync().get();
            return true;
        }
        catch (const Support::OS::Windows::Winrt::Exception &ex) {
            LOG(Warn, "_session.TryPauseAsync() failed. {}", Helper::ToString(ex));
            return false;
        }
    }

    std::wstring GetProgramName() const override
    {
        try {
            const auto appId = _session.SourceAppUserModelId();
            if (!appId.empty()) {
                return L"UniversalSystemSession(" + std::wstring{appId.c_str()} + L")";
            }
        }
        catch (const Support::OS::Windows::Winrt::Exception &) {
        }
        return L"UniversalSystemSession";
    }

    static std::vector<std::unique_ptr<MediaProgramAbstract>> EnumerateSessions()
    {
        std::vector<std::unique_ptr<MediaProgramAbstract>> result;
        try {
            const auto manager =
                GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            const auto sessions = manager.GetSessions();
            result.reserve(sessions.Size());
            for (uint32_t i = 0; i < sessions.Size(); ++i) {
                if (auto session = sessions.GetAt(i); session) {
                    result.emplace_back(
                        std::make_unique<UniversalSystemSession>(std::move(session)));
                }
            }
            if (result.empty()) {
                if (auto current = manager.GetCurrentSession(); current) {
                    result.emplace_back(
                        std::make_unique<UniversalSystemSession>(std::move(current)));
                }
            }
        }
        catch (const Support::OS::Windows::Winrt::Exception &ex) {
            LOG(Warn, "UniversalSystemSession enumerate sessions failed. Code: {:#x}, Message: {}",
                ex.code(), winrt::to_string(ex.message()));
        }
        return result;
    }

protected:
    Priority GetPriority() const override
    {
        return Priority::SystemSession;
    }

private:
    GlobalSystemMediaTransportControlsSession _session;

    static bool IsPlaying(const GlobalSystemMediaTransportControlsSession &gsmtcs)
    {
        try {
            return gsmtcs.GetPlaybackInfo().PlaybackStatus() ==
                   GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
        }
        catch (const Support::OS::Windows::Winrt::Exception &ex) {
            LOG(Warn, "UniversalSystemSession Get playback status failed. Code: {:#x}, Message: {}",
                ex.code(), winrt::to_string(ex.message()));
            return false;
        }
    }
};

class QQMusic final : public MediaProgramThroughVirtualKeyAbstract
{
public:
    std::wstring GetProgramName() const override
    {
        return L"QQMusic";
    }

protected:
    std::wstring GetProcessName() const override
    {
        return L"QQMusic.exe";
    }
    std::wstring GetWindowClassName() const override
    {
        return L"TXGuiFoundation";
    }
    WindowMatchingFlags GetWindowMatchingFlags() const override
    {
        return WindowMatchingFlag::HasChildren | WindowMatchingFlag::NonEmptyTitle;
    }
    Priority GetPriority() const override
    {
        return Priority::MusicPlayer;
    }
};

class NeteaseMusic final : public MediaProgramThroughVirtualKeyAbstract
{
public:
    std::wstring GetProgramName() const override
    {
        return L"NeteaseMusic";
    }

protected:
    std::wstring GetProcessName() const override
    {
        return L"cloudmusic.exe";
    }
    std::wstring GetWindowClassName() const override
    {
        return L"MiniPlayer";
    }
    WindowMatchingFlags GetWindowMatchingFlags() const override
    {
        return WindowMatchingFlag::NonEmptyTitle;
    }
    Priority GetPriority() const override
    {
        return Priority::MusicPlayer;
    }
};

class KuGouMusic final : public MediaProgramThroughVirtualKeyAbstract
{
public:
    std::wstring GetProgramName() const override
    {
        return L"KuGouMusic";
    }

protected:
    std::wstring GetProcessName() const override
    {
        return L"KuGou.exe";
    }
    std::wstring GetWindowClassName() const override
    {
        return L"kugou_ui";
    }
    WindowMatchingFlags GetWindowMatchingFlags() const override
    {
        return WindowMatchingFlag::NonEmptyTitle;
    }
    Priority GetPriority() const override
    {
        return Priority::MusicPlayer;
    }
};

auto GetAvailablePrograms()
{
    auto result = UniversalSystemSession::EnumerateSessions();

#define PUSH_IF_AVAILABLE(type)                                                                    \
    {                                                                                              \
        auto ptr = std::make_unique<type>();                                                       \
        if (ptr->IsAvailable()) {                                                                  \
            result.emplace_back(std::move(ptr));                                                   \
        }                                                                                          \
    }

    PUSH_IF_AVAILABLE(QQMusic);
    PUSH_IF_AVAILABLE(NeteaseMusic);
    PUSH_IF_AVAILABLE(KuGouMusic);

#undef PUSH_IF_AVAILABLE

    if (result.empty()) {
        return result;
    }

    // Sort by priority
    //
    std::sort(result.begin(), result.end(), [](const auto &first, const auto &second) {
        return first->GetPriority() < second->GetPriority();
    });

    return result;
}
} // namespace Details

Controller::~Controller()
{
    Shutdown();
}

void Controller::EnsureWorkerStartedLocked()
{
    if (_shuttingDown || _worker.joinable()) {
        return;
    }
    _worker = std::jthread{[this](std::stop_token stopToken) { WorkerLoop(stopToken); }};
}

void Controller::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock{_queueMutex};
        _shuttingDown = true;
        _pendingActions.clear();
    }
    _worker.request_stop();
    _queueCv.notify_all();
    if (_worker.joinable()) {
        _worker.join();
    }
}

void Controller::Play()
{
    std::lock_guard<std::mutex> lock{_queueMutex};
    if (_shuttingDown) {
        return;
    }
    EnsureWorkerStartedLocked();
    _pendingActions.push_back(Details::ActionId::Play);
    _queueCv.notify_one();
}

void Controller::Pause()
{
    std::lock_guard<std::mutex> lock{_queueMutex};
    if (_shuttingDown) {
        return;
    }
    EnsureWorkerStartedLocked();
    _pendingActions.push_back(Details::ActionId::Pause);
    _queueCv.notify_one();
}

void Controller::WorkerLoop(std::stop_token stopToken)
{
    // WinRT SMTC async calls and COM audio meters must run on an MTA thread rather than
    // blocking Qt's STA GUI thread.
    Support::OS::Windows::Winrt::Initialize();

    while (true) {
        Details::ActionId action;
        {
            std::unique_lock<std::mutex> lock{_queueMutex};
            _queueCv.wait(lock, [&] {
                return stopToken.stop_requested() || _shuttingDown || !_pendingActions.empty();
            });
            if (stopToken.stop_requested() || _shuttingDown) {
                break;
            }
            action = _pendingActions.front();
            _pendingActions.pop_front();
        }

        if (action == Details::ActionId::Pause) {
            ExecutePause();
        }
        else {
            ExecutePlay();
        }
    }

    // Release WinRT/COM proxies on the MTA thread that acquired them before exiting.
    _pausedPrograms.clear();
    _pausedAt.reset();
}

void Controller::ExecutePlay()
{
    if (_pausedPrograms.empty()) {
        LOG(Trace, L"Paused programs vector is empty.");
        return;
    }

    const bool expired = _pausedAt.has_value() && Clock::now() - _pausedAt.value() > kResumeExpiry;
    if (expired) {
        LOG(Info, "Skipping stale auto-resume: media was paused longer than {} minutes ago.",
            kResumeExpiry.count());
        _pausedPrograms.clear();
        _pausedAt.reset();
        return;
    }

    for (const auto &program : _pausedPrograms) {
        if (!program->Play()) {
            LOG(Warn, L"Failed to play media. Program name: {}", program->GetProgramName());
        }
        else {
            LOG(Trace, L"Media played. Program name: {}", program->GetProgramName());
        }
    }

    _pausedPrograms.clear();
    _pausedAt.reset();
}

void Controller::ExecutePause()
{
    auto programs = Details::GetAvailablePrograms();

    std::vector<std::unique_ptr<Details::MediaProgramAbstract>> newlyPaused;
    for (auto &&program : programs) {
        if (program->IsPlaying()) {
            if (!program->Pause()) {
                LOG(Warn, L"Failed to pause media. Program name: {}", program->GetProgramName());
            }
            else {
                LOG(Trace, L"Media paused. Program name: {}", program->GetProgramName());
                newlyPaused.emplace_back(std::move(program));
            }
        }
    }

    // Replace rather than append so disabling "Resume playback" never leaks sessions
    // across repeated pauses.
    if (!newlyPaused.empty()) {
        _pausedPrograms = std::move(newlyPaused);
        _pausedAt = Clock::now();
    }
}
} // namespace Core::GlobalMedia
