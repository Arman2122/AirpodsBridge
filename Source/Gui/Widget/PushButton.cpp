// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/PushButton.h"

#include <QLinearGradient>
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

void PushButton::SetPrimary(bool primary)
{
    if (_primary == primary) {
        return;
    }
    _primary = primary;
    update();
}

bool PushButton::IsPrimary() const
{
    return _primary;
}

void PushButton::SetPill(bool pill)
{
    if (_pill == pill) {
        return;
    }
    _pill = pill;
    update();
}

bool PushButton::IsPill() const
{
    return _pill;
}

void PushButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const auto &colours = Theme::Colours();
    const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;

    // Inset by half a pixel so the antialiased edge falls inside the widget and is not
    // clipped into a hard fringe by the widget boundary.
    const auto bounds = QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = _pill ? bounds.height() / 2.0 : Theme::Metric::ControlCornerRadius;

    QPainterPath shape;
    if (_pill) {
        shape.addRoundedRect(bounds, radius, radius);
    }
    else {
        shape = Painting::ContinuousRoundedRect(bounds, radius);
    }

    if (_primary && isEnabled()) {
        QColor top = colours.Accent.lighter(isDown() ? 94 : (underMouse() ? 120 : 114));
        QColor bottom = isDown() ? colours.Accent.darker(114)
                                 : (underMouse() ? colours.Accent.lighter(104) : colours.Accent);
        QLinearGradient fill{bounds.topLeft(), bounds.bottomLeft()};
        fill.setColorAt(0.0, top);
        fill.setColorAt(1.0, bottom);
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        painter.drawPath(shape);

        // Apple Liquid Glass subtle specular edge highlight
        QLinearGradient rim{bounds.topLeft(), bounds.bottomLeft()};
        rim.setColorAt(0.0, QColor{220, 242, 255, 195});
        rim.setColorAt(0.45, QColor{255, 255, 255, 42});
        rim.setColorAt(1.0, QColor{180, 225, 255, 130});
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{QBrush{rim}, 1.0});
        painter.drawPath(shape);
    }
    else {
        QColor background = _primary ? colours.Accent : colours.Button;
        if (!isEnabled()) {
            if (_primary) {
                background = colours.Accent;
                background.setAlpha(135);
            }
            else {
                background = colours.Control;
            }
        }
        else if (isDown()) {
            background = colours.ButtonPressed;
        }
        else if (underMouse()) {
            background = colours.ButtonHover;
        }

        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawPath(shape);

        if (_pill && isEnabled()) {
            QLinearGradient rim{bounds.topLeft(), bounds.bottomLeft()};
            rim.setColorAt(0.0, isDark ? QColor{255, 255, 255, 72} : QColor{255, 255, 255, 180});
            rim.setColorAt(1.0, isDark ? QColor{255, 255, 255, 26} : QColor{0, 0, 0, 22});
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{QBrush{rim}, 1.0});
            painter.drawPath(shape);
        }
    }

    const QColor textColour = _primary       ? colours.OnAccent
                              : !isEnabled() ? colours.Glyph
                                             : colours.TextPrimary;
    painter.setPen(textColour);
    painter.drawText(rect(), Qt::AlignCenter, text());
}

} // namespace Gui::Widget
