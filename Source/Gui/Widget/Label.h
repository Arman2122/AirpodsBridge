// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QLabel>

namespace Gui::Widget {

//
// A label whose colour is a role in the theme rather than a value.
//
// A colour written into a QLabel's palette sticks: it does not follow the application
// palette when the mode changes. So a label says which kind of text it is, and re-reads
// the theme on `Theme::ChangeEvent()`. Links are the accent in either mode, and a
// disabled label dims to the glyph grey by itself.
//
class Label : public QLabel
{
    Q_OBJECT

public:
    enum class Role {
        Primary,   // titles, and the body of anything that is the point
        Secondary, // descriptions, captions, and what explains the point
    };

    explicit Label(QWidget *parent = nullptr);
    Label(const QString &text, Role role, QWidget *parent = nullptr);

    void SetRole(Role role);

protected:
    bool event(QEvent *event) override;

private:
    Role _role{Role::Primary};

    void ApplyTheme();
};

} // namespace Gui::Widget
