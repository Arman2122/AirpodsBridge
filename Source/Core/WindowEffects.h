// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

class QWindow;

namespace Core::WindowEffects {

//
// Native window presentation, kept behind a platform boundary.
//
// The application used to round its own corners by clipping the window to a region --
// `SetWindowRgn` on Windows, `QWidget::setMask` otherwise. Both are one-bit masks: a pixel
// is either inside the window or outside it, with nothing in between, so every curve came
// out stair-stepped. `setMask` does not help even when the mask is painted with
// antialiasing on, because the result is thresholded back to one bit. Clipping with
// `SetWindowRgn` also opts the window out of the compositor's own rounding and shadow.
//
// So the rounding is handed to the compositor instead, which antialiases it properly and
// draws a real shadow underneath.
//

//
// Ask the compositor to round this window's corners.
//
// Returns true when the platform took responsibility for it. Returns false on Windows 10
// and earlier, where the attribute does not exist -- the correct response there is to
// leave the window square, which is what that platform's own flyouts look like, rather
// than to fall back to a mask and reintroduce the aliasing.
//
// The caller must have realised the native window already; `QWidget::winId()` forces
// `QWidget::windowHandle()` to become non-null.
//
bool ApplyRoundedCorners(QWindow *window);

//
// Ask the compositor for a dark title bar, or a light one again.
//
// Without this a dark application sits under a white title bar, which is the one part of
// the window it does not paint. On Windows 11 the same attribute also darkens the hairline
// the compositor draws around a rounded window, so the popup's edge follows too.
//
// Returns true when the compositor took it. The window must be realised already;
// `Gui::ThemeController` applies this to every window as it is created and again to all
// of them when the mode changes.
//
bool ApplyDarkTitleBar(QWindow *window, bool dark);

//
// Whether the user wants animations at all.
//
// Windows exposes this through the "Show animations in Windows" accessibility toggle. It
// is not a hint: an application that keeps animating after the user has turned this off is
// ignoring an accessibility setting, and for some users a motion trigger.
//
bool AreAnimationsEnabled();

//
// Round `preferredMs` to a whole number of refreshes of the display `window` is on.
//
// A duration that is not a multiple of the frame interval leaves the animation ending
// part-way through a frame, which reads as a stutter at the end of every open and close.
// The old code asked for a flat 500 ms, which lands mid-frame on 60 Hz, 120 Hz and 144 Hz
// alike, and never matched any of them.
//
// Returns 0 when animations are disabled, which Qt treats as "apply the end value
// immediately".
//
int AnimationDuration(const QWindow *window, int preferredMs);

} // namespace Core::WindowEffects
