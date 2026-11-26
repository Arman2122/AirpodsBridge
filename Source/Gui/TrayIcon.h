// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <functional>
#include <optional>

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QSystemTrayIcon>
#include <QTimer>

#include "Source/Core/AirPods.h"
#include "Source/Core/QuickConnect.h"
#include "Source/Core/Update.h"
#include "Source/Gui/SettingsWindow.h"
#include "Source/Gui/TrayActivation.h"
#include "Source/Presentation/Base.h"

namespace Gui {

class TrayIcon : public QWidget
{
    Q_OBJECT

public:
    explicit TrayIcon(Core::QuickConnect::Controller &quickConnect);

    template <class... ArgsT>
    inline void ShowMessage(ArgsT &&...args)
    {
        _tray->showMessage(std::forward<ArgsT>(args)...);
    }

    void UpdateState(const Core::AirPods::State &state);
    void Unavailable();
    void Disconnect();
    void Unbind();
    void SetDeviceConnected(bool connected);
    void VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo);
    void SetUpdateCheckFeedback(Core::Update::CheckStatus status, const QString &version = {});
    void RefreshQuickConnectMenu();
    void RefreshSettings();
    void OpenSettings(SettingsWindow::Section section = SettingsWindow::Section::General);
    void SetAudioEndpointsProvider(SettingsWindow::AudioEndpointsProvider provider);
    void SetDiagnosticsProvider(SettingsWindow::DiagnosticsProvider provider);
    void SetLatencyDiagnosticsProvider(SettingsWindow::LatencyDiagnosticsProvider provider);
    void SetRadioControlProvider(SettingsWindow::RadioControlProvider provider);
    void NotifyAudioEndpointsChanged();
    void NotifyLatencyDiagnosticsChanged();
    void NotifyRadioControlChanged();
    void SetRadioSetupBusy(bool busy, const QString &statusText = {});
    void SetRadioSetupProgress(int percent, const QString &statusText);
    void ShowContextMenu(const QPoint &position);

Q_SIGNALS:
    void OnTrayIconBatteryChangedSafely(Core::Settings::TrayIconBatteryBehavior value);
    void OnLowBatteryThresholdChangedSafely(uint32_t threshold);
    void ShowMainWindowRequested();
    void BindDeviceRequested();
    void ManualUpdateCheckRequested();
    void LatencyProfileSwitchRequested(bool lowLatencySbc);
    void RadioServiceActionRequested(Gui::SettingsWindow::RadioServiceAction action);
    void SwitchAudioOutputRequested();
    void NoiseControlModeRequested(Core::AirPods::NoiseControlMode mode);
    void ConversationalAwarenessRequested(bool enabled);
    void AdaptiveNoiseLevelRequested(uint8_t level);
    void OneBudAncRequested(bool enabled);
    void PersonalizedVolumeRequested(bool enabled);
    void LoudSoundReductionRequested(bool enabled);
    void HeadGesturesRequested(bool enabled);
    void MicrophoneSideRequested(Core::AirPods::MicrophoneSide side);
    void PressSpeedRequested(Core::AirPods::PressSpeed speed);
    void PressHoldDurationRequested(Core::AirPods::PressHoldDuration duration);
    void ToneVolumeRequested(uint8_t volumePercent);
    void AllowOffListeningModeRequested(bool allow);
    void HearingAidRequested(bool enabled);
    void MediaAssistRequested(bool enabled);
    void HeartRateMonitoringRequested(bool enabled);
    void UserUpdateRequested(const Core::Update::ReleaseInfo &releaseInfo);
    void ToolTipChanged(const QString &text);

private:
    struct IconRenderState {
        std::optional<QString> text;
        bool isLow{false};
        bool showUpdateDot{false};

        bool operator==(const IconRenderState &rhs) const = default;
    };

    QSystemTrayIcon *_tray = new QSystemTrayIcon{this};
    QMenu *_menu = new QMenu{this};
    QAction *_actionNewVersion = new QAction{tr("New version available!"), this};
    QAction *_actionQuickConnect = new QAction{tr("Connect AirPods"), this};
    QAction *_actionSwitchAudio = new QAction{tr("Switch audio to AirPods"), this};
    QMenu *_noiseControlMenu = new QMenu{tr("Noise control"), _menu};
    QActionGroup *_noiseControlGroup = new QActionGroup{this};
    QAction *_actionNoiseOff{nullptr};
    QAction *_actionNoiseAnc{nullptr};
    QAction *_actionNoiseTransparency{nullptr};
    QAction *_actionNoiseAdaptive{nullptr};
    QAction *_actionConversationalAwareness{nullptr};
    QAction *_actionSettings = new QAction{tr("Settings"), this};
    QAction *_actionAbout = new QAction{tr("About"), this};
    QAction *_actionQuit = new QAction{tr("Quit"), this};
    Core::Settings::TrayIconBatteryBehavior _trayIconBatteryBehavior{
        Core::Settings::TrayIconBatteryBehavior::Disable};
    uint32_t _lowBatteryThreshold{Core::AirPods::Battery::kDefaultLowThreshold};
    bool _deviceConnected{false};
    Status _status{Status::Unavailable};
    std::optional<Core::AirPods::State> _airPodsState;
    std::optional<Core::Update::ReleaseInfo> _updateReleaseInfo;
    std::optional<IconRenderState> _lastIconRenderState;
    Core::QuickConnect::Controller &_quickConnect;
    TrayActivationState _trayActivationState;
    QTimer _singleClickTimer{this};

    void ShowMainWindow();
    void Repaint();
    void UpdateDynamicMenus();
    void ExecuteTrayActivation(TrayActivationAction action);

    static QIcon GenerateMultiSizeIcon(
        const std::optional<QString> &optText, bool isLow, const std::optional<QColor> &dot);
    static QImage GenerateIcon(
        int size, const std::optional<QString> &optText, bool isLow,
        const std::optional<QColor> &dot);

    void OnNewVersionClicked();
    void OnSettingsClicked();
    void OnAboutClicked();
    void OnIconClicked(QSystemTrayIcon::ActivationReason reason);
    void OnQuickConnectOutcome(Core::QuickConnect::Outcome outcome, const QString &deviceName);
    void OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior value);
    void OnLowBatteryThresholdChanged(uint32_t threshold);

protected:
    SettingsWindow _settingsWindow;
};
} // namespace Gui
