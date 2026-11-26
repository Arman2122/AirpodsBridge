// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

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

// DWMWA_USE_IMMERSIVE_DARK_MODE. Documented as 20 from Windows 10 build 18985 (20H1); on
// 1809 through 1909, the oldest releases this application runs on, the same attribute
// lived at 19 undocumented, and the compositor honoured it there too.
constexpr DWORD kUseImmersiveDarkMode = 20;
constexpr DWORD kUseImmersiveDarkModeBefore20H1 = 19;

// DWMWA_SYSTEMBACKDROP_TYPE (Windows 11 22H2 / build 22621+).
constexpr DWORD kSystemBackdropType = 38;
constexpr DWORD kBackdropNone = 1;    // DWMSBT_NONE
constexpr DWORD kBackdropMica = 2;    // DWMSBT_MAINWINDOW
constexpr DWORD kBackdropAcrylic = 3; // DWMSBT_TRANSIENTWINDOW

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

bool ApplyDarkTitleBar(QWindow *window, bool dark)
{
    // `handle()` rather than `winId()`: the latter would create a native window for one
    // that has none, and this is also called as windows are being torn down.
    if (window == nullptr || window->handle() == nullptr) {
        return false;
    }

    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (handle == nullptr) {
        return false;
    }

    const BOOL value = dark ? TRUE : FALSE;
    for (const DWORD attribute : {kUseImmersiveDarkMode, kUseImmersiveDarkModeBefore20H1}) {
        if (SUCCEEDED(DwmSetWindowAttribute(handle, attribute, &value, sizeof(value)))) {
            return true;
        }
    }

    LOG(Info, "WindowEffects: the compositor does not take dark title bars here.");
    return false;
}

bool ApplyBackdrop(QWindow *window, BackdropStyle style)
{
    if (window == nullptr || window->handle() == nullptr) {
        return false;
    }

    const auto handle = reinterpret_cast<HWND>(window->winId());
    if (handle == nullptr) {
        return false;
    }

    DWORD value = kBackdropNone;
    switch (style) {
    case BackdropStyle::Mica:
        value = kBackdropMica;
        break;
    case BackdropStyle::Acrylic:
        value = kBackdropAcrylic;
        break;
    case BackdropStyle::None:
    default:
        value = kBackdropNone;
        break;
    }

    const HRESULT hr = DwmSetWindowAttribute(handle, kSystemBackdropType, &value, sizeof(value));
    if (FAILED(hr)) {
        return false;
    }

    if (style != BackdropStyle::None) {
        MARGINS margins{-1, -1, -1, -1};
        DwmExtendFrameIntoClientArea(handle, &margins);
    }
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
