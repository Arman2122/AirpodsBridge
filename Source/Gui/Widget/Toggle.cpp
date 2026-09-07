// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/Toggle.h"

#include <QPainter>
#include <QWindow>

#include "Source/Core/WindowEffects.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

namespace {

QColor Mix(const QColor &from, const QColor &to, qreal amount)
{
    return QColor::fromRgbF(
        from.redF() + (to.redF() - from.redF()) * amount,
        from.greenF() + (to.greenF() - from.greenF()) * amount,
        from.blueF() + (to.blueF() - from.blueF()) * amount);
}

} // namespace

Toggle::Toggle(QWidget *parent) : QAbstractButton{parent}
{
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

    _knobAnimation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&_knobAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        _knobPosition = value.toReal();
        update();
    });
}

QSize Toggle::sizeHint() const
{
    return {Theme::Metric::ToggleWidth, Theme::Metric::ToggleHeight};
}

void Toggle::checkStateSet()
{
    QAbstractButton::checkStateSet();
    MoveKnob();
}

void Toggle::nextCheckState()
{
    QAbstractButton::nextCheckState();
    MoveKnob();
}

void Toggle::MoveKnob()
{
    const qreal target = isChecked() ? 1.0 : 0.0;
    _knobAnimation.stop();

    // A window that is not on screen has nothing to animate for, and the value set while
    // the settings are being loaded into the form should land immediately.
    const auto *handle = window() != nullptr ? window()->windowHandle() : nullptr;
    const int duration =
        isVisible() && handle != nullptr
            ? Core::WindowEffects::AnimationDuration(handle, Theme::Duration::Toggle)
            : 0;
    if (duration <= 0) {
        _knobPosition = target;
        update();
        return;
    }

    _knobAnimation.setDuration(duration);
    _knobAnimation.setStartValue(_knobPosition);
    _knobAnimation.setEndValue(target);
    _knobAnimation.start();
}

void Toggle::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);

    // Half-pixel inset keeps the antialiased edge inside the widget; see PushButton.
    const auto bounds = QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = bounds.height() / 2.0;

    // The track fades between its two colours in step with the knob, so a frame caught
    // half-way through does not show an off-coloured track under an on-position knob.
    QColor track = Mix(Theme::Colours().ControlPressed, Theme::Colours().Accent, _knobPosition);
    QColor knob = Theme::Colours().OnAccent;
    if (!isEnabled()) {
        track.setAlphaF(0.45);
        knob.setAlphaF(0.8);
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(track);
    painter.drawRoundedRect(bounds, radius, radius);

    const qreal inset = 2.0;
    const qreal diameter = bounds.height() - inset * 2.0;
    const qreal travel = bounds.width() - inset * 2.0 - diameter;
    const QRectF knobRect{
        bounds.left() + inset + travel * _knobPosition, bounds.top() + inset, diameter, diameter};

    // A soft shadow under the knob is what makes it read as sitting on the track rather
    // than being printed on it.
    painter.setBrush(Theme::Colours().KnobShadow);
    painter.drawEllipse(knobRect.translated(0.0, 1.0));
    painter.setBrush(knob);
    painter.drawEllipse(knobRect);

    if (hasFocus()) {
        painter.setPen(QPen{Theme::Colours().Focus, 2.0});
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(bounds.adjusted(1.0, 1.0, -1.0, -1.0), radius, radius);
    }
}

} // namespace Gui::Widget
