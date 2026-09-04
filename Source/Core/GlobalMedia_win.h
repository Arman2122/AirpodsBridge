// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

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

#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <functional>

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
    friend Helper::Singleton<Controller>;

public:
    void Play() override;
    void Pause() override;

private:
    std::mutex _mutex;
    std::vector<std::unique_ptr<Details::MediaProgramAbstract>> _pausedPrograms;
};
} // namespace Core::GlobalMedia
