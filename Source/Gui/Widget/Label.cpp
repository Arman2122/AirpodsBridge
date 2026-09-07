// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/Label.h"

#include <QEvent>

#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

Label::Label(QWidget *parent) : QLabel{parent}
{
    ApplyTheme();
}

Label::Label(const QString &text, Role role, QWidget *parent) : QLabel{text, parent}, _role{role}
{
    ApplyTheme();
}

void Label::SetRole(Role role)
{
    if (_role == role) {
        return;
    }
    _role = role;
    ApplyTheme();
}

bool Label::event(QEvent *event)
{
    if (event->type() == Theme::ChangeEvent()) {
        ApplyTheme();
    }
    return QLabel::event(event);
}

void Label::ApplyTheme()
{
    const auto &colours = Theme::Colours();

    auto palette = this->palette();
    palette.setColor(
        QPalette::WindowText,
        _role == Role::Secondary ? colours.TextSecondary : colours.TextPrimary);
    // QLabel picks the Disabled group itself, so dimming needs no event handling.
    palette.setColor(QPalette::Disabled, QPalette::WindowText, colours.Glyph);
    palette.setColor(QPalette::Link, colours.Accent);
    setPalette(palette);
}

} // namespace Gui::Widget
