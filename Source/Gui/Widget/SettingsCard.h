// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

namespace Gui::Widget {

//
// One setting: a title, an optional line explaining it, and the control on the right.
//
// The explanation is shown, not hidden behind a hover tip. A setting the user has to
// guess at is a setting they will leave alone.
//
class SettingsRow : public QWidget
{
    Q_OBJECT

public:
    SettingsRow(
        const QString &title, const QString &description, QWidget *control,
        QWidget *parent = nullptr);

    void SetTitle(const QString &title);
    void SetDescription(const QString &description);

private:
    QLabel *_title;
    QLabel *_description;
};

//
// A card holding related rows, with a heading above it.
//
// Rows are separated by a hairline that stops short of the card's edge, the way grouped
// lists are drawn on iOS and in Windows 11's Settings, so the group reads as one object
// rather than a stack of boxes.
//
class SettingsGroup : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsGroup(const QString &heading, QWidget *parent = nullptr);

    SettingsRow *AddRow(const QString &title, const QString &description, QWidget *control);

    // For content that is not a setting -- the About page's text, for instance.
    void AddWidget(QWidget *widget);

private:
    class Card;

    Card *_card;
};

} // namespace Gui::Widget
