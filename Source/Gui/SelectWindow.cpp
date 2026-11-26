// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

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
    // `setCurrentRow(0)` sets both the visual selection and `currentRow()`, so confirming
    // the pre-selected first entry without re-clicking it returns index 0 rather than -1.
    _ui.listWidget->setCurrentRow(0);

    connect(_ui.listWidget, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        _clickedButton = QDialogButtonBox::Yes;
        accept();
    });
}

bool SelectWindow::HasResult() const
{
    return _clickedButton == QDialogButtonBox::Yes && GetSelectedIndex() >= 0;
}

int SelectWindow::GetSelectedIndex() const
{
    return _ui.listWidget->currentRow();
}

void SelectWindow::OnButtonClicked(QDialogButtonBox::StandardButton button)
{
    _clickedButton = button;
}
} // namespace Gui
