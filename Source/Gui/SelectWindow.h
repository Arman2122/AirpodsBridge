// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QDialog>
#include <QString>
#include <QStringList>

#include "ui_SelectWindow.h"

#include "Source/Gui/Utils.h"

namespace Gui {

class SelectWindow : public QDialog
{
    Q_OBJECT

public:
    SelectWindow(const QString &title, const QStringList &items, QWidget *parent = nullptr);

    bool HasResult() const;
    int GetSelectedIndex() const;

private:
    Ui::SelectWindow _ui;
    QDialogButtonBox::StandardButton _clickedButton = QDialogButtonBox::NoButton;

    void OnButtonClicked(QDialogButtonBox::StandardButton button);

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};
} // namespace Gui
