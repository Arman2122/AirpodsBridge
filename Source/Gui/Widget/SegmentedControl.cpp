// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/SegmentedControl.h"

#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "Source/Gui/Painting.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

namespace {

// Space around the label inside a segment, and between the segments and the track edge.
constexpr int kSegmentPadding = 14;
constexpr qreal kTrackInset = 2.0;

} // namespace

SegmentedControl::SegmentedControl(QWidget *parent) : QWidget{parent}
{
    setFont(Theme::Font::Body());
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void SegmentedControl::AddSegment(const QString &text)
{
    _segments.append(text);
    if (_currentIndex < 0) {
        _currentIndex = 0;
    }
    updateGeometry();
    update();
}

int SegmentedControl::CurrentIndex() const
{
    return _currentIndex;
}

void SegmentedControl::SetCurrentIndex(int index)
{
    if (index < 0 || index >= _segments.size() || index == _currentIndex) {
        return;
    }
    _currentIndex = index;
    update();
    Q_EMIT CurrentIndexChanged(index);
}

QFont SegmentedControl::SelectedFont() const
{
    auto font = this->font();
    font.setWeight(QFont::DemiBold);
    return font;
}

QSize SegmentedControl::sizeHint() const
{
    // Measured in the selected weight so a segment never grows when it is chosen.
    const QFontMetrics metrics{SelectedFont()};
    int widest = 0;
    for (const auto &segment : _segments) {
        widest = qMax(widest, metrics.horizontalAdvance(segment));
    }

    const int segmentWidth = widest + kSegmentPadding * 2;
    const int width = segmentWidth * _segments.size() + static_cast<int>(kTrackInset * 2);
    return {width, Theme::Metric::SegmentHeight};
}

QRectF SegmentedControl::SegmentRect(int index) const
{
    if (_segments.isEmpty()) {
        return {};
    }
    const qreal inner = width() - kTrackInset * 2.0;
    const qreal segmentWidth = inner / _segments.size();
    return {
        kTrackInset + segmentWidth * index, kTrackInset, segmentWidth,
        height() - kTrackInset * 2.0};
}

int SegmentedControl::SegmentAt(const QPoint &position) const
{
    for (int i = 0; i < _segments.size(); ++i) {
        if (SegmentRect(i).contains(position)) {
            return i;
        }
    }
    return -1;
}

void SegmentedControl::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int index = SegmentAt(event->pos());
    if (index >= 0) {
        SetCurrentIndex(index);
    }
}

void SegmentedControl::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Left:
        SetCurrentIndex(_currentIndex - 1);
        break;
    case Qt::Key_Right:
        SetCurrentIndex(_currentIndex + 1);
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void SegmentedControl::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const auto bounds = QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal radius = Theme::Metric::SegmentCornerRadius;

    painter.setPen(Qt::NoPen);
    painter.setBrush(Theme::Colours().Control);
    painter.drawPath(Painting::ContinuousRoundedRect(bounds, radius));

    if (_currentIndex >= 0) {
        // The chosen segment is a raised pill: a whisper of shadow, then a surface lifted
        // above the track -- white in light, a lighter grey in dark.
        const auto pill = SegmentRect(_currentIndex);
        const qreal pillRadius = radius - kTrackInset;
        painter.setBrush(Theme::Colours().KnobShadow);
        painter.drawPath(Painting::ContinuousRoundedRect(pill.translated(0.0, 1.0), pillRadius));
        painter.setBrush(isEnabled() ? Theme::Colours().Raised : Theme::Colours().ControlHover);
        painter.drawPath(Painting::ContinuousRoundedRect(pill, pillRadius));
    }

    const QColor text = isEnabled() ? Theme::Colours().TextPrimary : Theme::Colours().Glyph;
    for (int i = 0; i < _segments.size(); ++i) {
        painter.setFont(i == _currentIndex ? SelectedFont() : font());
        painter.setPen(text);
        painter.drawText(SegmentRect(i), Qt::AlignCenter, _segments.at(i));
    }

    if (hasFocus()) {
        painter.setPen(QPen{Theme::Colours().Focus, 2.0});
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(Painting::ContinuousRoundedRect(bounds.adjusted(1, 1, -1, -1), radius));
    }
}

} // namespace Gui::Widget
