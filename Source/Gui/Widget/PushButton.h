// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QPushButton>

namespace Gui::Widget {

//
// The application's action button.
//
// It paints itself rather than being styled with a stylesheet, because `border-radius` in
// a stylesheet can only produce a circular corner, and the shape this project wants is a
// continuous one. See `Source/Gui/Painting.h` for why that difference is visible.
//
// Colours and the corner radius come from `Gui::Theme`, so this class carries no palette
// of its own.
//
class PushButton : public QPushButton
{
    Q_OBJECT

public:
    explicit PushButton(QWidget *parent = nullptr);

    void SetPrimary(bool primary);
    bool IsPrimary() const;
    void SetPill(bool pill);
    bool IsPill() const;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    bool _primary{false};
    bool _pill{false};
};

} // namespace Gui::Widget
