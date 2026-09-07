// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QStringList>
#include <QWidget>

namespace Gui::Widget {

//
// A row of mutually exclusive choices, one of which is always selected.
//
// This replaces a row of radio buttons for the small enumerations in the settings -- three
// ways to show a battery, say. Every segment is the same width, sized to the widest label
// in its bold weight so that selecting one does not shift the others.
//
class SegmentedControl : public QWidget
{
    Q_OBJECT

public:
    explicit SegmentedControl(QWidget *parent = nullptr);

    void AddSegment(const QString &text);
    int CurrentIndex() const;
    void SetCurrentIndex(int index);

    QSize sizeHint() const override;

Q_SIGNALS:
    void CurrentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    QStringList _segments;
    int _currentIndex{-1};

    QFont SelectedFont() const;
    QRectF SegmentRect(int index) const;
    int SegmentAt(const QPoint &position) const;
};

} // namespace Gui::Widget
