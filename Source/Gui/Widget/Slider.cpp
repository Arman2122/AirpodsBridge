// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/Slider.h"

#include <QPainter>
#include <QStyleOptionSlider>
#include <QWheelEvent>

#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

Slider::Slider(QWidget *parent) : QSlider{Qt::Horizontal, parent}
{
    setCursor(Qt::PointingHandCursor);
    // Tab focus keeps a click from taking focus; the wheel is handled in `wheelEvent`,
    // because QSlider moves on it whether focused or not.
    setFocusPolicy(Qt::TabFocus);
}

QSize Slider::sizeHint() const
{
    auto hint = QSlider::sizeHint();
    hint.setHeight(Theme::Metric::SliderKnobDiameter + 6);
    return hint;
}

void Slider::wheelEvent(QWheelEvent *event)
{
    // On a page that scrolls, a wheel over the slider is a scroll, not a new value. The
    // tick is left for the page unless the slider was reached by keyboard.
    if (!hasFocus()) {
        event->ignore();
        return;
    }
    QSlider::wheelEvent(event);
}

void Slider::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QStyleOptionSlider option;
    initStyleOption(&option);

    const QRect groove =
        style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderGroove, this);
    const QRect handle =
        style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, this);

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing);

    const qreal trackHeight = Theme::Metric::SliderTrackHeight;
    const qreal centreY = groove.center().y() + 0.5;
    const qreal knobRadius = Theme::Metric::SliderKnobDiameter / 2.0;
    // The knob's own radius keeps it inside the widget at either end of the range.
    const qreal left = groove.left() + knobRadius;
    const qreal right = groove.right() - knobRadius;
    const qreal knobX = qBound(left, handle.center().x() + 0.5, right);

    QColor track = Theme::Colours().Divider;
    QColor fill = Theme::Colours().Accent;
    QColor knob = Theme::Colours().OnAccent;
    if (!isEnabled()) {
        fill = Theme::Colours().ControlPressed;
        knob.setAlphaF(0.8);
    }

    painter.setPen(Qt::NoPen);

    const QRectF trackRect{left, centreY - trackHeight / 2.0, right - left, trackHeight};
    painter.setBrush(track);
    painter.drawRoundedRect(trackRect, trackHeight / 2.0, trackHeight / 2.0);

    const QRectF fillRect{left, trackRect.top(), knobX - left, trackHeight};
    painter.setBrush(fill);
    painter.drawRoundedRect(fillRect, trackHeight / 2.0, trackHeight / 2.0);

    const QRectF knobRect{knobX - knobRadius, centreY - knobRadius, knobRadius * 2, knobRadius * 2};
    painter.setBrush(Theme::Colours().KnobShadow);
    painter.drawEllipse(knobRect.translated(0.0, 1.0));
    painter.setBrush(knob);
    painter.drawEllipse(knobRect);

    if (hasFocus()) {
        painter.setPen(QPen{Theme::Colours().Focus, 2.0});
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(knobRect.adjusted(-2, -2, 2, 2));
    }
}

} // namespace Gui::Widget
