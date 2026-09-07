// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <thread>

#include <QDialog>

#include "ui_MainWindow.h"

#include <QPropertyAnimation>

#include "Source/Gui/Utils.h"
#include "Source/Presentation/MainWindowPresentation.h"
#include "Source/Core/AirPods.h"
#include "Source/Core/Update.h"
#include "Source/Presentation/Base.h"
#include "Source/Gui/Widget/Battery.h"
#include "Source/Gui/Widget/DeviceView.h"

namespace Gui {

class CloseButton;
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
    // The user chose "Remind me later" on an update prompt.
    void UpdateDeferred();

private:
    constexpr static QSize _windowSize{320, 300};
    constexpr static QSize _screenMargin{24, 24};
    constexpr static int _deviceLabelMaximumPointSize = 18;
    constexpr static int _deviceLabelMinimumPointSize = 12;

    Ui::MainWindow _ui;

    QPropertyAnimation _posAnimation{this, "pos"};
    Widget::DeviceView *_deviceView = new Widget::DeviceView{this};
    QTimer *_autoHideTimer = new QTimer{this};
    CloseButton *_closeButton;
    Widget::Battery *_leftBattery = new Widget::Battery{this};
    Widget::Battery *_rightBattery = new Widget::Battery{this};
    Widget::Battery *_caseBattery = new Widget::Battery{this};

    ButtonAction _buttonAction{ButtonAction::NoButton};
    MainWindowViewModel _viewModel;
    // Where the form put the battery containers: used when there is no artwork to place
    // them by.
    QPoint _podsHome, _caseHome;
    bool _isVisible{false};
    std::atomic<bool> _deviceQueryRunning{false};
    std::jthread _deviceQueryThread;

    void ChangeButtonAction(ButtonAction action);
    void BindDevice();
    void ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices);
    void ControlAutoHideTimer(bool start);
    void Repaint();
    void PlaceBatteries(const std::optional<DeviceArtwork::Artwork> &artwork);
    void FitDeviceLabelFont();

    void OnAppStateChanged(Qt::ApplicationState state);
    void OnPosMoveFinished();
    void OnAnimationClicked();
    void OnButtonClicked();

    void DoHide();
    void showEvent(QShowEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};
} // namespace Gui
