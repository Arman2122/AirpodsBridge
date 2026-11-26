// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QDialog>

#include "Source/Gui/Utils.h"

namespace Gui {

namespace Widget {
class Label;
class ProgressBar;
class PushButton;
class Toggle;
} // namespace Widget

//
// Apple-style first-run setup dialog.
//
// Enables the No-Test-Mode Control Engine, Nearby AirPods Popup, and Start with Windows
// by default so users get the complete Apple experience immediately while keeping every
// toggle clear and reversible in Settings at any time.
//
class WelcomeDialog : public QDialog
{
    Q_OBJECT

public:
    struct Result {
        bool autoRun{true};
        bool showNearbyPopup{true};
        bool controlEngineEnabled{false};
        bool rebootRequired{false};
    };

    explicit WelcomeDialog(bool serviceAlreadyRunning, QWidget *parent = nullptr);

    Result GetResult() const;

private:
    bool _serviceAlreadyRunning{false};
    bool _setupRunning{false};
    Result _result;

    Widget::Toggle *_controlEngineToggle{nullptr};
    Widget::Toggle *_nearbyPopupToggle{nullptr};
    Widget::Toggle *_autoRunToggle{nullptr};
    Widget::ProgressBar *_progressBar{nullptr};
    Widget::Label *_statusLabel{nullptr};
    Widget::PushButton *_continueButton{nullptr};
    Widget::PushButton *_skipEngineButton{nullptr};

    void OnContinueClicked();
    void FinishAndAccept();
    void closeEvent(QCloseEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};

} // namespace Gui
