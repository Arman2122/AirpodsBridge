// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/PushButton.h"

#include <QPainter>

#include "Source/Gui/Painting.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

PushButton::PushButton(QWidget *parent) : QPushButton{parent}
{
    setFont(Theme::Font::Button());
    setCursor(Qt::PointingHandCursor);
    // The default style would draw a frame underneath everything painted below.
    setFlat(true);
}

void PushButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    QColor background = Theme::Colours().Button;
    if (!isEnabled()) {
        background = Theme::Colours().Control;
    }
    else if (isDown()) {
        background = Theme::Colours().ButtonPressed;
    }
    else if (underMouse()) {
        background = Theme::Colours().ButtonHover;
    }

    // Inset by half a pixel so the antialiased edge falls inside the widget and is not
    // clipped into a hard fringe by the widget boundary.
    const auto bounds = QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5);

    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawPath(Painting::ContinuousRoundedRect(bounds, Theme::Metric::ControlCornerRadius));

    painter.setPen(isEnabled() ? Theme::Colours().TextPrimary : Theme::Colours().Glyph);
    painter.drawText(rect(), Qt::AlignCenter, text());
}

} // namespace Gui::Widget
