// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#if !defined APB_OS_WIN
    #error "This file shouldn't be compiled."
#endif

#include <Windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "Source/Core/GlobalMedia_abstract.h"
#include "Source/Support/OS/Windows.h"

namespace Core::GlobalMedia {
namespace Details {

enum class ActionId : uint32_t { Play, Pause };

class MediaProgramAbstract
{
public:
    enum class Priority : uint32_t {
        Max = 0,

        SystemSession = 1,
        MusicPlayer = 2,

        Min = std::numeric_limits<uint32_t>::max()
    };

    virtual inline ~MediaProgramAbstract() {};

    virtual bool IsAvailable() = 0;
    virtual bool IsPlaying() const = 0;
    virtual bool Play() = 0;
    virtual bool Pause() = 0;

    virtual std::wstring GetProgramName() const = 0;
    virtual Priority GetPriority() const = 0;
};
} // namespace Details

class Controller final : public Helper::Singleton<Controller>, public Details::ControllerAbstract
{
protected:
    Controller() = default;
    ~Controller() override;
    friend Helper::Singleton<Controller>;

public:
    void Play() override;
    void Pause() override;
    void Shutdown();

private:
    using Clock = std::chrono::steady_clock;
    // Do not auto-resume media that was paused more than 30 minutes ago.
    static constexpr std::chrono::minutes kResumeExpiry{30};

    std::mutex _queueMutex;
    std::condition_variable _queueCv;
    std::deque<Details::ActionId> _pendingActions;
    bool _shuttingDown{false};
    std::jthread _worker;

    // Owned exclusively by `_worker` (MTA apartment).
    std::vector<std::unique_ptr<Details::MediaProgramAbstract>> _pausedPrograms;
    std::optional<Clock::time_point> _pausedAt;

    void EnsureWorkerStartedLocked();
    void WorkerLoop(std::stop_token stopToken);
    void ExecutePlay();
    void ExecutePause();
};
} // namespace Core::GlobalMedia
