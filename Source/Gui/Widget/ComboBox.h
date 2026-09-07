// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QComboBox>

namespace Gui::Widget {

//
// A drop-down list drawn with the application's own box and chevron.
//
// QComboBox keeps its model, its popup and its keyboard handling; only the closed state is
// painted here. A stylesheet can recolour the box but cannot draw a chevron without an
// image resource, which is why this is code like the other controls.
//
class ComboBox : public QComboBox
{
    Q_OBJECT

public:
    explicit ComboBox(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    void ApplyTheme();
};

} // namespace Gui::Widget
