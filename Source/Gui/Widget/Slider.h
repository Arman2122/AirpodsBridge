// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QSlider>

namespace Gui::Widget {

//
// A horizontal slider drawn with the application's own track and knob.
//
// QSlider keeps its range, keyboard handling and mouse mathematics; the style's idea of
// where the handle is comes from `subControlRect`, so the drawing and the hit-testing
// agree even though the visuals are ours.
//
class Slider : public QSlider
{
    Q_OBJECT

public:
    explicit Slider(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
};

} // namespace Gui::Widget
