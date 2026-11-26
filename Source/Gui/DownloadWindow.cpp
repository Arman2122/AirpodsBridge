// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/DownloadWindow.h"

#include <algorithm>

#include <QMessageBox>
#include <QPushButton>

#include <Config.h>
#include "Source/Gui/Utils.h"

namespace Gui {

DownloadWindow::DownloadWindow(Core::Update::ReleaseInfo info, QWidget *parent)
    : QDialog{parent}, _info{std::move(info)}
{
    _ui.setupUi(this);

    // 0..1000 permille scale avoids integer overflow on large assets and keeps the bar smooth.
    _ui.progressBar->setRange(0, 1000);
    _ui.progressBar->setValue(0);

    connect(_ui.pushButtonDownloadManually, &QPushButton::clicked, this, [this]() {
        _downloadThread.request_stop();
        _info.OpenUrl();
        reject();
    });

    connect(this, &DownloadWindow::UpdateProgressSafely, this, &DownloadWindow::UpdateProgress);
    connect(this, &DownloadWindow::OnSucceededSafely, this, &DownloadWindow::OnSucceeded);
    connect(this, &DownloadWindow::OnFailedSafely, this, &DownloadWindow::OnFailed);

    _downloadThread =
        std::jthread{[this](std::stop_token stopToken) { DownloadThread(stopToken); }};
}

DownloadWindow::~DownloadWindow()
{
    _downloadThread.request_stop();
    if (_downloadThread.joinable()) {
        _downloadThread.join();
    }
}

void DownloadWindow::UpdateProgress(int progressPermille)
{
    _ui.progressBar->setValue(std::clamp(progressPermille, 0, 1000));
}

void DownloadWindow::OnSucceeded()
{
    // The installer has already launched detached; caller quits gracefully on Accepted.
    accept();
}

void DownloadWindow::OnFailed()
{
    LOG(Warn, "DownloadInstall failed. Offering manual release page.");

    QMessageBox::warning(
        this, Config::ProgramName,
        tr("Oops, an error occurred during the automatic update.\n"
           "Opening the release page so you can download it manually."));

    _info.OpenUrl();
    reject();
}

void DownloadWindow::DownloadThread(std::stop_token stopToken)
{
    const bool successful =
        Core::Update::DownloadInstall(_info, [this, stopToken](size_t downloaded, size_t total) {
            if (stopToken.stop_requested()) {
                LOG(Info, "Download cancelled because the window is closing.");
                return false;
            }

            if (total > 0) {
                const auto permille = static_cast<int>(
                    std::min<uint64_t>(1000, (static_cast<uint64_t>(downloaded) * 1000) / total));
                emit UpdateProgressSafely(permille);
            }
            return true;
        });

    if (stopToken.stop_requested()) {
        return;
    }

    if (successful) {
        emit OnSucceededSafely();
    }
    else {
        emit OnFailedSafely();
    }
}
} // namespace Gui
