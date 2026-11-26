// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/WelcomeDialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPointer>
#include <QVBoxLayout>

#include <Config.h>
#if defined APB_OS_WIN
    #include "Source/Core/AirPodsServiceTransport_win.h"
#endif
#include "Source/Gui/Widget/Label.h"
#include "Source/Gui/Widget/ProgressBar.h"
#include "Source/Gui/Widget/PushButton.h"
#include "Source/Gui/Widget/SettingsCard.h"
#include "Source/Gui/Widget/Toggle.h"
#include "Source/Presentation/Theme.h"

namespace Gui {

WelcomeDialog::WelcomeDialog(bool serviceAlreadyRunning, QWidget *parent)
    : QDialog{parent}, _serviceAlreadyRunning{serviceAlreadyRunning}
{
    setWindowTitle(tr("Welcome to %1").arg(Config::ProgramName));
    setFixedWidth(480);

    auto *root = new QVBoxLayout{this};
    root->setContentsMargins(24, 24, 24, 20);
    root->setSpacing(16);

    // Header: icon + title + concise subtitle
    auto *header = new QHBoxLayout;
    header->setSpacing(14);

    auto *icon = new QLabel{this};
    icon->setPixmap(qApp->windowIcon().pixmap(48, 48));
    icon->setFixedSize(48, 48);
    header->addWidget(icon, 0, Qt::AlignTop);

    auto *titleCol = new QVBoxLayout;
    titleCol->setContentsMargins(0, 0, 0, 0);
    titleCol->setSpacing(4);

    auto *title = new Widget::Label{
        tr("Welcome to %1").arg(Config::ProgramName), Widget::Label::Role::Primary, this};
    title->setFont(Theme::Font::Heading());

    auto *subtitle = new Widget::Label{
        tr("Apple-style AirPods battery, Noise Control, and automatic ear detection on Windows."),
        Widget::Label::Role::Secondary, this};
    subtitle->setFont(Theme::Font::Body());
    subtitle->setWordWrap(true);

    titleCol->addWidget(title);
    titleCol->addWidget(subtitle);
    header->addLayout(titleCol, 1);
    root->addLayout(header);

    // Setup card with all recommended capabilities ON by default
    auto *group = new Widget::SettingsGroup{tr("Recommended setup"), this};

    _controlEngineToggle = new Widget::Toggle;
    _controlEngineToggle->setChecked(true);
    const QString engineDesc =
        serviceAlreadyRunning
            ? tr("Already active. Provides full Noise Control, 1% battery, and stereo mic with "
                 "Secure Boot on. You can disable it anytime in Settings.")
            : tr("Unlocks Apple Noise Control, 1% battery, Conversation Awareness, and stereo mic "
                 "with Secure Boot on. Installs Microsoft-signed usbip-win2 silently if needed "
                 "(USB devices may briefly reconnect once). You can disable it anytime in "
                 "Settings.");
    group->AddRow(tr("AirPods Control Engine (No Test Mode)"), engineDesc, _controlEngineToggle);

    _nearbyPopupToggle = new Widget::Toggle;
    _nearbyPopupToggle->setChecked(true);
    group->AddRow(
        tr("Nearby AirPods popup"),
        tr("Shows live battery rings and one-click Connect even when your AirPods are connected "
           "to another device such as an iPhone or iPad."),
        _nearbyPopupToggle);

    _autoRunToggle = new Widget::Toggle;
    _autoRunToggle->setChecked(true);
    group->AddRow(
        tr("Launch when Windows starts"),
        tr("Starts quietly in the system tray when you sign in to Windows."), _autoRunToggle);

    root->addWidget(group);

    // Embedded silent progress section (hidden until setup runs)
    _progressBar = new Widget::ProgressBar{this};
    _progressBar->hide();
    root->addWidget(_progressBar);

    _statusLabel = new Widget::Label{{}, Widget::Label::Role::Secondary, this};
    _statusLabel->setFont(Theme::Font::Footnote());
    _statusLabel->setWordWrap(true);
    _statusLabel->hide();
    root->addWidget(_statusLabel);

    // Footer buttons
    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(0, 4, 0, 0);
    footer->setSpacing(10);

    _skipEngineButton = new Widget::PushButton{this};
    _skipEngineButton->setText(tr("Continue without engine"));
    _skipEngineButton->setFixedHeight(34);
    _skipEngineButton->setMinimumWidth(168);
    _skipEngineButton->hide();
    connect(_skipEngineButton, &QPushButton::clicked, this, [this] {
        _result.controlEngineEnabled = false;
        FinishAndAccept();
    });

    _continueButton = new Widget::PushButton{this};
    _continueButton->setText(tr("Get Started"));
    _continueButton->SetPrimary(true);
    _continueButton->SetPill(true);
    _continueButton->setFixedHeight(36);
    _continueButton->setMinimumWidth(136);
    connect(_continueButton, &QPushButton::clicked, this, &WelcomeDialog::OnContinueClicked);

    footer->addWidget(_skipEngineButton);
    footer->addStretch();
    footer->addWidget(_continueButton);
    root->addLayout(footer);

    adjustSize();
}

WelcomeDialog::Result WelcomeDialog::GetResult() const
{
    return _result;
}

void WelcomeDialog::OnContinueClicked()
{
    if (_setupRunning) {
        return;
    }

    _result.autoRun = _autoRunToggle->isChecked();
    _result.showNearbyPopup = _nearbyPopupToggle->isChecked();

    if (!_controlEngineToggle->isChecked()) {
        _result.controlEngineEnabled = false;
        FinishAndAccept();
        return;
    }

    if (_serviceAlreadyRunning) {
        _result.controlEngineEnabled = true;
        FinishAndAccept();
        return;
    }

#if defined APB_OS_WIN
    _setupRunning = true;
    _controlEngineToggle->setEnabled(false);
    _nearbyPopupToggle->setEnabled(false);
    _autoRunToggle->setEnabled(false);
    _skipEngineButton->hide();
    _continueButton->setEnabled(false);
    _continueButton->setText(tr("Setting up..."));

    _progressBar->SetProgress(5);
    _progressBar->show();
    _statusLabel->setText(
        tr("Approve the Windows administrator prompt to enable the Control Engine..."));
    _statusLabel->show();
    adjustSize();

    QPointer<WelcomeDialog> self{this};
    Core::AirPods::ServiceControlTransport::RunElevatedSetupAsync(
        false, false,
        [self](Core::AirPods::ServiceControlTransport::SetupOutcome outcome) {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, outcome = std::move(outcome)]() {
                    if (!self) {
                        return;
                    }
                    self->_setupRunning = false;
                    if (outcome.succeeded) {
                        self->_progressBar->SetProgress(100);
                        self->_result.controlEngineEnabled = true;
                        self->_result.rebootRequired = outcome.rebootRequired;
                        self->_statusLabel->setText(QString::fromStdString(outcome.message));
                        self->FinishAndAccept();
                        return;
                    }

                    self->_controlEngineToggle->setEnabled(true);
                    self->_nearbyPopupToggle->setEnabled(true);
                    self->_autoRunToggle->setEnabled(true);
                    self->_continueButton->setEnabled(true);
                    self->_continueButton->setText(tr("Retry"));
                    self->_skipEngineButton->show();
                    self->_statusLabel->setText(
                        outcome.cancelled
                            ? tr("Administrator prompt was cancelled. Click Retry or continue "
                                 "without the Control Engine (you can enable it later in "
                                 "Settings).")
                            : tr("Setup did not finish: %1")
                                  .arg(QString::fromStdString(outcome.message)));
                    self->adjustSize();
                },
                Qt::QueuedConnection);
        },
        [self](int percent, std::string message) {
            if (!self) {
                return;
            }
            QMetaObject::invokeMethod(
                self,
                [self, percent, msg = QString::fromStdString(message)]() {
                    if (!self || !self->_setupRunning) {
                        return;
                    }
                    self->_progressBar->SetProgress(percent);
                    if (!msg.isEmpty()) {
                        self->_statusLabel->setText(msg);
                    }
                },
                Qt::QueuedConnection);
        });
#else
    _result.controlEngineEnabled = false;
    FinishAndAccept();
#endif
}

void WelcomeDialog::FinishAndAccept()
{
    accept();
}

void WelcomeDialog::closeEvent(QCloseEvent *event)
{
    if (_setupRunning) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

} // namespace Gui
