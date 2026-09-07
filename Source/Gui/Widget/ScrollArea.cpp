// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/ScrollArea.h"

#include <QApplication>
#include <QEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWindow>

#include "Source/Core/WindowEffects.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

namespace {

constexpr qreal kDegreesPerNotch = 120.0;
// Windows' "one screen at a time" wheel setting reaches Qt as an outsized line count.
constexpr int kMaxWheelLines = 100;

} // namespace

ScrollArea::ScrollArea(QWidget *parent) : QScrollArea{parent}
{
    _glide.setEasingCurve(QEasingCurve::OutCubic);
    connect(&_glide, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        verticalScrollBar()->setValue(value.toInt());
    });
    connect(verticalScrollBar(), &QScrollBar::sliderPressed, &_glide, &QVariantAnimation::stop);

    ApplyTheme();
}

void ScrollArea::ApplyTheme()
{
    const auto &colours = Theme::Colours();
    verticalScrollBar()->setStyleSheet(
        QStringLiteral(
            "QScrollBar:vertical { background: transparent; width: 10px; margin: 4px 2px; }"
            "QScrollBar::handle:vertical { background: %1; border-radius: 3px;"
            "  min-height: 32px; }"
            "QScrollBar::handle:vertical:hover { background: %2; }"
            "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
            "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical"
            "  { background: transparent; }")
            .arg(colours.ControlPressed.name(), colours.Glyph.name()));
}

bool ScrollArea::event(QEvent *event)
{
    if (event->type() == Theme::ChangeEvent()) {
        ApplyTheme();
    }
    return QScrollArea::event(event);
}

void ScrollArea::wheelEvent(QWheelEvent *event)
{
    auto *bar = verticalScrollBar();
    const int duration = GlideDuration();

    if (!event->pixelDelta().isNull() || event->angleDelta().y() == 0 || duration <= 0 ||
        bar->minimum() == bar->maximum())
    {
        QScrollArea::wheelEvent(event);
        return;
    }

    // The target survives from one notch to the next only while the glide is running;
    // otherwise the bar may have been moved by hand since.
    if (_glide.state() != QAbstractAnimation::Running) {
        _target = bar->value();
    }
    const qreal notches = event->angleDelta().y() / kDegreesPerNotch;
    _target = qBound(bar->minimum(), _target - qRound(notches * PixelsPerNotch()), bar->maximum());

    // Restarting from wherever the bar is keeps the motion continuous: the curve starts
    // fast, so a notch that lands mid-glide carries the speed on rather than resetting it.
    _glide.stop();
    if (_target != bar->value()) {
        _glide.setDuration(duration);
        _glide.setStartValue(bar->value());
        _glide.setEndValue(_target);
        _glide.start();
    }
    event->accept();
}

int ScrollArea::GlideDuration() const
{
    const auto *handle = window() != nullptr ? window()->windowHandle() : nullptr;
    return handle != nullptr
               ? Core::WindowEffects::AnimationDuration(handle, Theme::Duration::Scroll)
               : 0;
}

int ScrollArea::PixelsPerNotch() const
{
    const int lines = QApplication::wheelScrollLines();
    if (lines <= 0 || lines > kMaxWheelLines) {
        return verticalScrollBar()->pageStep();
    }
    return lines * Theme::Metric::ScrollLine;
}

} // namespace Gui::Widget
