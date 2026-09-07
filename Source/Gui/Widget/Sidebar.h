// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QVector>
#include <QWidget>

namespace Gui::Widget {

//
// The navigation column of the settings window: one entry per page.
//
// A painted list rather than a QListWidget so that the selection and hover shapes use the
// same continuous corners as everything else, and so that an entry can be hidden without
// leaving a gap -- the Debug page only exists in debug builds.
//
class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget *parent = nullptr);

    int AddItem(const QString &text);
    void SetItemVisible(int index, bool visible);
    int CurrentIndex() const;
    void SetCurrentIndex(int index);

    QSize sizeHint() const override;

Q_SIGNALS:
    void CurrentIndexChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    struct Item {
        QString text;
        bool visible{true};
    };

    QVector<Item> _items;
    int _currentIndex{-1};
    int _hoverIndex{-1};

    QRectF ItemRect(int index) const;
    int ItemAt(const QPoint &position) const;
    int Step(int from, int direction) const;
};

} // namespace Gui::Widget
