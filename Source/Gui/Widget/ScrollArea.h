// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QScrollArea>
#include <QVariantAnimation>

namespace Gui::Widget {

//
// A scroll area whose mouse wheel scrolls smoothly.
//
// A wheel notch does not jump the content by a step; it moves the target, and the view
// glides there on the same curve as the toggle knob. Notches that arrive while the glide is
// under way extend it, so a flick of the wheel reads as one motion. A precision touchpad
// reports pixels rather than notches and is already smooth, so it takes the stock path; so
// does a system with animations turned off. The scrollbar itself stays direct, and taking
// hold of it ends any glide.
//
// The scrollbar is the one piece of chrome Qt draws inside a page, so it is styled here:
// thin, and on the palette rather than the style's default.
//
class ScrollArea : public QScrollArea
{
    Q_OBJECT

public:
    explicit ScrollArea(QWidget *parent = nullptr);

protected:
    bool event(QEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    QVariantAnimation _glide;
    int _target{0};

    void ApplyTheme();
    int GlideDuration() const;
    int PixelsPerNotch() const;
};

} // namespace Gui::Widget
