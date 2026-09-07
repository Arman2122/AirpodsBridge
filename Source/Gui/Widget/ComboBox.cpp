// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/ComboBox.h"

#include <QEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

#include "Source/Gui/Painting.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

namespace {

constexpr int kTextPadding = 10;
constexpr int kChevronWidth = 26;

} // namespace

ComboBox::ComboBox(QWidget *parent) : QComboBox{parent}
{
    setFont(Theme::Font::Body());
    setCursor(Qt::PointingHandCursor);
    // Tab focus keeps a click from taking focus; the wheel is handled in `wheelEvent`,
    // because QComboBox changes its selection on it whether focused or not.
    setFocusPolicy(Qt::TabFocus);

    ApplyTheme();
}

void ComboBox::ApplyTheme()
{
    // The popup is the one part that stays a stock view; this keeps it on the palette. A
    // stylesheet bakes the colours in, hence redoing it on a change of mode.
    const auto &colours = Theme::Colours();
    setStyleSheet(QStringLiteral("QComboBox QAbstractItemView { background: %1; color: %2;"
                                 "  border: 1px solid %3; padding: 4px; outline: none;"
                                 "  selection-background-color: %4; selection-color: %5; }")
                      .arg(
                          colours.Surface.name(), colours.TextPrimary.name(),
                          colours.Divider.name(), colours.Accent.name(), colours.OnAccent.name()));
}

bool ComboBox::event(QEvent *event)
{
    if (event->type() == Theme::ChangeEvent()) {
        ApplyTheme();
    }
    return QComboBox::event(event);
}

QSize ComboBox::sizeHint() const
{
    auto hint = QComboBox::sizeHint();
    hint.setHeight(Theme::Metric::SegmentHeight);
    return hint;
}

void ComboBox::wheelEvent(QWheelEvent *event)
{
    // On a page that scrolls, a wheel over the list is a scroll, not a new choice. The
    // tick is left for the page unless the list was reached by keyboard.
    if (!hasFocus()) {
        event->ignore();
        return;
    }
    QComboBox::wheelEvent(event);
}

void ComboBox::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const auto bounds = QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5);

    QColor background = Theme::Colours().Control;
    if (isEnabled() && underMouse()) {
        background = Theme::Colours().ControlHover;
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    painter.drawPath(Painting::ContinuousRoundedRect(bounds, Theme::Metric::SegmentCornerRadius));

    const QColor ink = isEnabled() ? Theme::Colours().TextPrimary : Theme::Colours().Glyph;

    const auto textRect = bounds.adjusted(kTextPadding, 0, -kChevronWidth, 0);
    painter.setPen(ink);
    painter.setFont(font());
    painter.drawText(
        textRect, Qt::AlignLeft | Qt::AlignVCenter,
        fontMetrics().elidedText(
            currentText(), Qt::ElideRight, static_cast<int>(textRect.width())));

    // The chevron: two strokes, round-capped, the same weight as the close button's cross.
    const QPointF centre{bounds.right() - kChevronWidth / 2.0, bounds.center().y()};
    QPainterPath chevron;
    chevron.moveTo(centre.x() - 4.0, centre.y() - 2.0);
    chevron.lineTo(centre.x(), centre.y() + 2.0);
    chevron.lineTo(centre.x() + 4.0, centre.y() - 2.0);
    painter.setPen(QPen{
        isEnabled() ? Theme::Colours().Glyph : Theme::Colours().ControlPressed, 1.7, Qt::SolidLine,
        Qt::RoundCap, Qt::RoundJoin});
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chevron);

    if (hasFocus()) {
        painter.setPen(QPen{Theme::Colours().Focus, 2.0});
        painter.drawPath(Painting::ContinuousRoundedRect(
            bounds.adjusted(1, 1, -1, -1), Theme::Metric::SegmentCornerRadius));
    }
}

} // namespace Gui::Widget
