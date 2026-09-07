// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/Sidebar.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

#include "Source/Gui/Painting.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

namespace {

constexpr int kItemHeight = 32;
constexpr int kItemSpacing = 2;
constexpr int kEdgeInset = 10;
constexpr int kTopInset = 12;
constexpr int kTextInset = 12;
constexpr qreal kItemRadius = 7.0;

} // namespace

Sidebar::Sidebar(QWidget *parent) : QWidget{parent}
{
    setFont(Theme::Font::Body());
    setFocusPolicy(Qt::TabFocus);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

int Sidebar::AddItem(const QString &text)
{
    _items.append(Item{text});
    if (_currentIndex < 0) {
        _currentIndex = 0;
    }
    update();
    return _items.size() - 1;
}

void Sidebar::SetItemVisible(int index, bool visible)
{
    if (index < 0 || index >= _items.size()) {
        return;
    }
    _items[index].visible = visible;
    update();
}

int Sidebar::CurrentIndex() const
{
    return _currentIndex;
}

void Sidebar::SetCurrentIndex(int index)
{
    if (index < 0 || index >= _items.size() || !_items.at(index).visible || index == _currentIndex)
    {
        return;
    }
    _currentIndex = index;
    update();
    Q_EMIT CurrentIndexChanged(index);
}

QSize Sidebar::sizeHint() const
{
    return {Theme::Metric::SidebarWidth, 0};
}

QRectF Sidebar::ItemRect(int index) const
{
    // Hidden entries take no space, so the position is the count of visible ones before it.
    int slot = 0;
    for (int i = 0; i < index; ++i) {
        if (_items.at(i).visible) {
            ++slot;
        }
    }
    const qreal y = kTopInset + slot * (kItemHeight + kItemSpacing);
    return {
        static_cast<qreal>(kEdgeInset), y, static_cast<qreal>(width() - kEdgeInset * 2),
        static_cast<qreal>(kItemHeight)};
}

int Sidebar::ItemAt(const QPoint &position) const
{
    for (int i = 0; i < _items.size(); ++i) {
        if (_items.at(i).visible && ItemRect(i).contains(position)) {
            return i;
        }
    }
    return -1;
}

int Sidebar::Step(int from, int direction) const
{
    for (int i = from + direction; i >= 0 && i < _items.size(); i += direction) {
        if (_items.at(i).visible) {
            return i;
        }
    }
    return from;
}

void Sidebar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const int index = ItemAt(event->pos());
    if (index >= 0) {
        SetCurrentIndex(index);
    }
}

void Sidebar::mouseMoveEvent(QMouseEvent *event)
{
    const int index = ItemAt(event->pos());
    if (index != _hoverIndex) {
        _hoverIndex = index;
        setCursor(index >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void Sidebar::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    if (_hoverIndex != -1) {
        _hoverIndex = -1;
        update();
    }
}

void Sidebar::keyPressEvent(QKeyEvent *event)
{
    switch (event->key()) {
    case Qt::Key_Up:
        SetCurrentIndex(Step(_currentIndex, -1));
        break;
    case Qt::Key_Down:
        SetCurrentIndex(Step(_currentIndex, +1));
        break;
    default:
        QWidget::keyPressEvent(event);
    }
}

void Sidebar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    painter.fillRect(rect(), Theme::Colours().Sidebar);

    auto selectedFont = font();
    selectedFont.setWeight(QFont::DemiBold);

    for (int i = 0; i < _items.size(); ++i) {
        const auto &item = _items.at(i);
        if (!item.visible) {
            continue;
        }

        const auto bounds = ItemRect(i);
        const bool selected = i == _currentIndex;

        if (selected || i == _hoverIndex) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(
                selected ? Theme::Colours().SidebarSelected : Theme::Colours().Control);
            painter.drawPath(Painting::ContinuousRoundedRect(bounds, kItemRadius));
        }

        painter.setFont(selected ? selectedFont : font());
        painter.setPen(Theme::Colours().TextPrimary);
        painter.drawText(
            bounds.adjusted(kTextInset, 0, -kTextInset, 0), Qt::AlignLeft | Qt::AlignVCenter,
            item.text);

        if (selected && hasFocus()) {
            painter.setPen(QPen{Theme::Colours().Focus, 2.0});
            painter.setBrush(Qt::NoBrush);
            painter.drawPath(
                Painting::ContinuousRoundedRect(bounds.adjusted(1, 1, -1, -1), kItemRadius));
        }
    }
}

} // namespace Gui::Widget
