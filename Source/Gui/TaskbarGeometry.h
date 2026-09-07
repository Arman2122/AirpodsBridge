// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <algorithm>
#include <cmath>

#include <QRect>
#include <QSize>

namespace Gui::TaskbarGeometry {

struct Layout {
    QRect statusLogical;
    QRect taskButtonsNative;
};

constexpr inline int kDefaultDpi = 96;

inline int NativeToLogical(int value, int dpi)
{
    const auto effectiveDpi = dpi > 0 ? dpi : kDefaultDpi;
    return static_cast<int>(std::lround(static_cast<double>(value) * kDefaultDpi / effectiveDpi));
}

inline int LogicalToNative(int value, int dpi)
{
    const auto effectiveDpi = dpi > 0 ? dpi : kDefaultDpi;
    return static_cast<int>(std::lround(static_cast<double>(value) * effectiveDpi / kDefaultDpi));
}

inline Layout CalculateLayout(
    const QSize &rebarNativeSize, const QRect &taskButtonsNative, int dpi, bool horizontal,
    const QSize &statusLogicalSize = {60, 40})
{
    auto adjustedTaskButtons = taskButtonsNative;
    QRect status;

    if (horizontal) {
        const auto statusNativeWidth = LogicalToNative(statusLogicalSize.width(), dpi);
        adjustedTaskButtons.setWidth(
            std::max(0, rebarNativeSize.width() - statusNativeWidth - taskButtonsNative.left()));
        status = {
            NativeToLogical(rebarNativeSize.width(), dpi) - statusLogicalSize.width(), 0,
            statusLogicalSize.width(), NativeToLogical(taskButtonsNative.height(), dpi)};
    }
    else {
        const auto statusNativeHeight = LogicalToNative(statusLogicalSize.height(), dpi);
        adjustedTaskButtons.setHeight(
            std::max(0, rebarNativeSize.height() - statusNativeHeight - taskButtonsNative.top()));
        status = {
            0, NativeToLogical(rebarNativeSize.height(), dpi) - statusLogicalSize.height(),
            NativeToLogical(taskButtonsNative.width(), dpi), statusLogicalSize.height()};
    }

    return {.statusLogical = status, .taskButtonsNative = adjustedTaskButtons};
}

inline QRect
RestoreTaskButtons(const QSize &rebarNativeSize, const QRect &taskButtonsNative, bool horizontal)
{
    auto restored = taskButtonsNative;
    if (horizontal) {
        restored.setWidth(std::max(0, rebarNativeSize.width() - taskButtonsNative.left()));
    }
    else {
        restored.setHeight(std::max(0, rebarNativeSize.height() - taskButtonsNative.top()));
    }
    return restored;
}

} // namespace Gui::TaskbarGeometry
