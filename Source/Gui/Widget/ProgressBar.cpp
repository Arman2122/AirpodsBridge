// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/ProgressBar.h"

#include <algorithm>

#include <QPainter>
#include <QWindow>

#include "Source/Core/WindowEffects.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

ProgressBar::ProgressBar(QWidget *parent) : QWidget{parent}
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(8);

    _animation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&_animation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        _displayFraction = v.toReal();
        update();
    });
}

void ProgressBar::SetProgress(int percent)
{
    const int clamped = std::clamp(percent, 0, 100);
    _targetPercent = clamped;
    const qreal target = static_cast<qreal>(clamped) / 100.0;

    _animation.stop();
    const auto *handle = window() != nullptr ? window()->windowHandle() : nullptr;
    const int duration =
        isVisible() && handle != nullptr
            ? Core::WindowEffects::AnimationDuration(handle, Theme::Duration::Toggle)
            : 0;
    if (duration <= 0) {
        _displayFraction = target;
        update();
        return;
    }

    _animation.setDuration(duration);
    _animation.setStartValue(_displayFraction);
    _animation.setEndValue(target);
    _animation.start();
}

int ProgressBar::Progress() const
{
    return _targetPercent;
}

QSize ProgressBar::sizeHint() const
{
    return {240, 8};
}

void ProgressBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto &colours = Theme::Colours();
    const QRectF track = QRectF{rect()}.adjusted(0.5, 1.0, -0.5, -1.0);
    const qreal radius = track.height() / 2.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(colours.ControlPressed);
    painter.drawRoundedRect(track, radius, radius);

    if (_displayFraction > 0.005) {
        const qreal fillW = std::max(track.height(), track.width() * _displayFraction);
        const QRectF fill{
            track.left(), track.top(), std::min(track.width(), fillW), track.height()};
        painter.setBrush(colours.Accent);
        painter.drawRoundedRect(fill, radius, radius);
    }
}

} // namespace Gui::Widget
