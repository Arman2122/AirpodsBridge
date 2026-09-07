// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <thread>
#include <QDialog>

#include "Source/Core/Update.h"
#include "Source/Gui/Utils.h"
#include "ui_DownloadWindow.h"

namespace Gui {

class DownloadWindow : public QDialog
{
    Q_OBJECT

public:
    DownloadWindow(Core::Update::ReleaseInfo info, QWidget *parent = nullptr);
    ~DownloadWindow();

Q_SIGNALS:
    void UpdateProgressSafely(int downloaded, int total);
    void OnFailedSafely();

private:
    Ui::DownloadWindow _ui;

    Core::Update::ReleaseInfo _info;
    std::jthread _downloadThread;

    void UpdateProgress(int downloaded, int total);
    void OnFailed();

    void DownloadThread(std::stop_token stopToken);

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};
} // namespace Gui
