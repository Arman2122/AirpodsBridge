// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <atomic>
#include <thread>

#include <QDialog>

#include "ui_MainWindow.h"

#include <QVideoWidget>
#include <QMediaPlayer>
#include <QPropertyAnimation>

#include "Source/Gui/Utils.h"
#include "Source/Presentation/MainWindowPresentation.h"
#include "Source/Core/AirPods.h"
#include "Source/Core/Update.h"
#include "Source/Presentation/Base.h"
#include "Source/Gui/Widget/Battery.h"

namespace Gui {

class CloseButton;
class VideoWidget;
class BatteryInfo;

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void UpdateState(const Core::AirPods::State &state);
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();
    void AskUserUpdate(const Core::Update::ReleaseInfo &releaseInfo);

Q_SIGNALS:
    void UpdateStateSafely(const Core::AirPods::State &state);
    void AvailableSafely();
    void UnavailableSafely();
    void DisconnectSafely();
    void BindSafely();
    void UnbindSafely();
    void ShowSafely();
    void HideSafely();
    bool VersionUpdateAvailableSafely(const Core::Update::ReleaseInfo &releaseInfo, bool silent);
    void SilentUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo);

private:
    constexpr static QSize _windowSize{320, 300};
    constexpr static QSize _screenMargin{24, 24};
    constexpr static int _deviceLabelMaximumPointSize = 18;
    constexpr static int _deviceLabelMinimumPointSize = 12;

    Ui::MainWindow _ui;

    QPropertyAnimation _posAnimation{this, "pos"};
    VideoWidget *_videoWidget;
    QMediaPlayer *_mediaPlayer = new QMediaPlayer{this};
    QTimer *_autoHideTimer = new QTimer{this};
    CloseButton *_closeButton;
    Widget::Battery *_leftBattery = new Widget::Battery{this};
    Widget::Battery *_rightBattery = new Widget::Battery{this};
    Widget::Battery *_caseBattery = new Widget::Battery{this};

    Core::Update::AsyncChecker _updateChecker{[this](auto &&...args) {
        VersionUpdateAvailableSafely(std::forward<decltype(args)>(args)...);
    }};
    std::optional<Core::AirPods::Model> _cacheModel;
    ButtonAction _buttonAction{ButtonAction::NoButton};
    MainWindowViewModel _viewModel;
    bool _isVisible{false};
    bool _isAnimationPlaying{false};
    std::atomic<bool> _deviceQueryRunning{false};
    std::jthread _deviceQueryThread;

    void ChangeButtonAction(ButtonAction action);
    void SetAnimation(std::optional<Core::AirPods::Model> model);
    void PlayAnimation();
    void StopAnimation();
    void BindDevice();
    void ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices);
    void ControlAutoHideTimer(bool start);
    void VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo, bool silent);
    void Repaint();
    void FitDeviceLabelFont();

    void OnAppStateChanged(Qt::ApplicationState state);
    void OnPosMoveFinished();
    void OnAnimationClicked();
    void OnButtonClicked();
    void OnPlayerStateChanged(QMediaPlayer::State newState);

    void DoHide();
    void showEvent(QShowEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};
} // namespace Gui
