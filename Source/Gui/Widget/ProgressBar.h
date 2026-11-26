// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QVariantAnimation>
#include <QWidget>

namespace Gui::Widget {

//
// Slim Apple-style horizontal progress bar (0..100%) driven by FrameClock.
// Reads `Theme::Colours()` on every paintEvent so Light/Dark switches apply immediately.
//
class ProgressBar : public QWidget
{
    Q_OBJECT

public:
    explicit ProgressBar(QWidget *parent = nullptr);

    void SetProgress(int percent);
    int Progress() const;

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int _targetPercent{0};
    qreal _displayFraction{0.0};
    QVariantAnimation _animation;
};

} // namespace Gui::Widget
