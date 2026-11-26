// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

#include <QDialog>
#include <QPropertyAnimation>

#include "ui_MainWindow.h"

#include "Source/Core/AirPods.h"
#include "Source/Core/Update.h"
#include "Source/Gui/Utils.h"
#include "Source/Gui/Widget/Battery.h"
#include "Source/Gui/Widget/DeviceView.h"
#include "Source/Presentation/Base.h"
#include "Source/Presentation/MainWindowPresentation.h"

namespace Gui {

class CloseButton;
class ControlsButton;

namespace Widget {
class AppleNoiseControl;
class Label;
class PopupHeartRateBar;
} // namespace Widget

class MainWindow : public QDialog
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void UpdateState(const Core::AirPods::State &state);
    void UpdateNearbyState(const Core::AirPods::State &state, bool shouldAutoShow);
    void ClearNearby();
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();
    void SetBoundDevice(Core::AirPods::Model model, const QString &displayName);
    void ShowPopup(bool activate = false);
    void ToggleVisibility();
    void BindDevice();
    void SetLowBatteryThreshold(uint32_t threshold);
    void SetQuickConnectAvailable(bool available);
    void SetShowHeartRateInPopup(bool show);
    void SetConnecting(bool connecting);
    void SetTransientStatus(const QString &status);
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
    void QuickConnectRequested();
    void NoiseControlModeRequested(Core::AirPods::NoiseControlMode mode);
    void AdaptiveNoiseLevelRequested(uint8_t level);
    void ConversationalAwarenessRequested(bool enabled);
    void PersonalizedVolumeRequested(bool enabled);
    void HearingAidRequested(bool enabled);
    void HeartRateMonitoringRequested(bool enabled);
    void OpenControlSettingsRequested();
    void OpenHealthSettingsRequested();
    // The user chose "Remind me later" on an update prompt.
    void UpdateDeferred();

private:
    constexpr static int _shadowMargin = 10;
    constexpr static qreal _cardCornerRadius = 28.0;
    constexpr static QSize _windowSize{364, 312};
    constexpr static QSize _nearbyWindowSize{364, 346};
    constexpr static QSize _expandedWindowSize{364, 472};
    constexpr static QSize _expandedWithHeartRateWindowSize{364, 528};
    constexpr static QSize _compactWithHeartRateWindowSize{364, 340};
    constexpr static QSize _screenMargin{14, 14};
    constexpr static int _deviceLabelMaximumPointSize = 18;
    constexpr static int _deviceLabelMinimumPointSize = 12;

    Ui::MainWindow _ui;

    QPropertyAnimation _posAnimation{this, "pos"};
    Widget::DeviceView *_deviceView = new Widget::DeviceView{this};
    QTimer *_autoHideTimer = new QTimer{this};
    QTimer *_transientStatusTimer = new QTimer{this};
    CloseButton *_closeButton;
    ControlsButton *_controlsButton;
    Widget::Label *_statusBadge{nullptr};
    Widget::AppleNoiseControl *_noiseControl{nullptr};
    Widget::PopupHeartRateBar *_heartRateBar{nullptr};
    Widget::Battery *_leftBattery = new Widget::Battery{this};
    Widget::Battery *_rightBattery = new Widget::Battery{this};
    Widget::Battery *_caseBattery = new Widget::Battery{this};

    ButtonAction _buttonAction{ButtonAction::NoButton};
    MainWindowViewModel _viewModel;
    // Where the form put the battery containers: used when there is no artwork to place
    // them by.
    QPoint _podsHome, _caseHome;
    QRect _buttonHome;
    bool _isVisible{false};
    bool _isHovered{false};
    std::chrono::steady_clock::time_point _nearbyDismissedUntil{};
    std::atomic<bool> _deviceQueryRunning{false};
    std::jthread _deviceQueryThread;

    void ChangeButtonAction(ButtonAction action, bool nearbyLayout, bool connecting);
    void ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices);
    void ControlAutoHideTimer(bool start);
    void Repaint();
    void UpdateLayoutAndControls(const MainWindowPresentation &presentation);
    void AnchorToCurrentScreen();
    void PlaceBatteries(const std::optional<DeviceArtwork::Artwork> &artwork);
    void FitDeviceLabelFont();

    void OnAppStateChanged(Qt::ApplicationState state);
    void OnPosMoveFinished();
    void OnAnimationClicked();
    void OnButtonClicked();

    void DoHide(bool userDismissed = false);
    void paintEvent(QPaintEvent *event) override;
    void enterEvent(QEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};
} // namespace Gui
