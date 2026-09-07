// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QAbstractButton>
#include <QVariantAnimation>

namespace Gui::Widget {

//
// A switch: the control for a setting that is either on or off.
//
// It is a QAbstractButton so that clicking, keyboard handling and the `toggled` signal all
// come from Qt; only the drawing lives here. The knob slides between its two positions,
// but only when the user has animations on -- see Core::WindowEffects::AnimationDuration.
//
class Toggle : public QAbstractButton
{
    Q_OBJECT

public:
    explicit Toggle(QWidget *parent = nullptr);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    // 0 puts the knob at the left (off), 1 at the right (on).
    qreal _knobPosition{0.0};
    QVariantAnimation _knobAnimation;

    void MoveKnob();
};

} // namespace Gui::Widget
