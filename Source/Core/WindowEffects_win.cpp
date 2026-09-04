// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/Core/WindowEffects.h"

#include <algorithm>
#include <cmath>

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>

#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"

// Must follow Windows.h, which Source/Support/OS/Windows.h brings in.
#include <dwmapi.h>

namespace Core::WindowEffects {
namespace {

//
// Declared here rather than taken from the SDK headers: the constant and the enumeration
// arrived with the Windows 11 SDK, and this project is built against an SDK that may
// predate it. The value is stable and documented.
//
// https://learn.microsoft.com/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute
//
constexpr DWORD kWindowCornerPreference = 33; // DWMWA_WINDOW_CORNER_PREFERENCE
constexpr DWORD kCornerPreferenceRound = 2;   // DWMWCP_ROUND

} // namespace

bool ApplyRoundedCorners(QWindow *window)
{
    if (window == nullptr) {
        return false;
    }

    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (handle == nullptr) {
        return false;
    }

    // A window that is still clipped to a region cannot be rounded by the compositor, and
    // an old region may be left over from a previous version's settings. Clearing it is
    // harmless when none is set.
    SetWindowRgn(handle, nullptr, TRUE);

    DWORD preference = kCornerPreferenceRound;
    const HRESULT result =
        DwmSetWindowAttribute(handle, kWindowCornerPreference, &preference, sizeof(preference));
    if (FAILED(result)) {
        // Expected on Windows 10, which returns E_INVALIDARG for an attribute it does not
        // know. Square corners there match that platform's own flyouts.
        LOG(Info, "WindowEffects: compositor rounding unavailable (hr: {:#x}), leaving square.",
            static_cast<uint32_t>(result));
        return false;
    }

    LOG(Info, "WindowEffects: compositor is rounding the window.");
    return true;
}

bool AreAnimationsEnabled()
{
    BOOL enabled = TRUE;
    if (SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) == FALSE) {
        // Assume the user wants animations if the setting cannot be read; silently
        // disabling motion would be the more surprising failure.
        return true;
    }
    return enabled != FALSE;
}

int AnimationDuration(const QWindow *window, int preferredMs)
{
    if (preferredMs <= 0 || !AreAnimationsEnabled()) {
        return 0;
    }

    const QScreen *screen = window != nullptr ? window->screen() : nullptr;
    if (screen == nullptr) {
        screen = QGuiApplication::primaryScreen();
    }

    const qreal refreshRate = screen != nullptr ? screen->refreshRate() : 0.0;
    if (refreshRate <= 1.0) {
        return preferredMs;
    }

    const qreal frameMs = 1000.0 / refreshRate;
    const auto frames = std::max<int>(1, static_cast<int>(std::lround(preferredMs / frameMs)));
    return static_cast<int>(std::lround(frames * frameMs));
}

} // namespace Core::WindowEffects
