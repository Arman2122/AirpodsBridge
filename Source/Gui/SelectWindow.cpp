// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/Gui/SelectWindow.h"

#include <QPushButton>

namespace Gui {

SelectWindow::SelectWindow(const QString &title, const QStringList &items, QWidget *parent)
    : QDialog{parent}
{
    _ui.setupUi(this);

    const auto &connectButton = [this](QDialogButtonBox::StandardButton button) {
        connect(_ui.buttonBox->button(button), &QPushButton::clicked, this, [this, button]() {
            OnButtonClicked(button);
        });
    };

    APB_ASSERT(!items.isEmpty());

    connectButton(QDialogButtonBox::Yes);
    connectButton(QDialogButtonBox::Cancel);

    _ui.label->setText(title);
    _ui.listWidget->addItems(items);
    _ui.listWidget->item(0)->setSelected(true);
}

bool SelectWindow::HasResult() const
{
    return _clickedButton == QDialogButtonBox::Yes;
}

int SelectWindow::GetSeletedIndex() const
{
    return _ui.listWidget->currentRow();
}

void SelectWindow::OnButtonClicked(QDialogButtonBox::StandardButton button)
{
    _clickedButton = button;
}
} // namespace Gui
