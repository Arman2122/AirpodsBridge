// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <functional>
#include <vector>

#include <QDialog>
#include <QLabel>
#include <QStackedWidget>
#include <QTextEdit>

#include "Source/Core/AirPodsTransport.h"
#include "Source/Core/AudioControl.h"
#include "Source/Core/QuickConnect.h"
#include "Source/Core/Settings.h"
#include "Source/Core/Update.h"
#include "Source/Domain/AirPodsState.h"
#include "Source/Gui/Utils.h"

namespace Gui {

namespace Widget {
class AppleNoiseControl;
class ComboBox;
class HeartRateCard;
class ProgressBar;
class PushButton;
class SegmentedControl;
class SettingsRow;
class Sidebar;
class Slider;
class Toggle;
} // namespace Widget

//
// The settings window: a sidebar of pages, each a column of cards.
//
// Every control is bound to one field of `Core::Settings::Fields` at construction. A
// change writes through `ModifiableAccess`, which saves and applies it; `Refresh` pushes
// the stored values back into the controls with their signals blocked, so loading never
// echoes as a write. Adding a setting is one row and one `Bind` call.
//
class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    enum class Section : size_t {
        General,
        Display,
        Control,
        Health,
        Audio,
        Features,
        About,
        Debug
    };

    enum class RadioServiceAction : uint8_t {
        InstallOrEnable = 0,
        Uninstall = 1,
        RetryGuard = 2,
        RestoreNative = 3,
    };

    struct DiagnosticsInfo {
        QString transportSummary;
        QString connectionSummary;
        QString clipboardReport;
    };

    struct RadioControlInfo {
        QString engineHeadline;
        QString engineDetail;
        QString adapterSummary;
        bool serviceInstalled{false};
        bool serviceRunning{false};
        bool guardTripped{false};
        bool aapConnected{false};
        std::optional<uint8_t> heartRateServiceId;
        std::optional<Core::AirPods::State> state;
    };

    using AudioEndpointsProvider = std::function<std::vector<Core::AudioControl::AudioEndpoint>()>;
    using DiagnosticsProvider = std::function<DiagnosticsInfo()>;
    using LatencyDiagnosticsProvider = std::function<Core::AudioControl::LatencyDiagnostics()>;
    using RadioControlProvider = std::function<RadioControlInfo()>;

    explicit SettingsWindow(
        Core::QuickConnect::Controller &quickConnect, QWidget *parent = nullptr);

    Section CurrentSection() const;
    void SelectSection(Section section);
    void RefreshBindingState();
    void RefreshAudioDevices();
    void RefreshDiagnostics();
    void RefreshLatencyDiagnostics();
    void RefreshRadioControl();
    void SetRadioSetupBusy(bool busy, const QString &statusText = {});
    void SetRadioSetupProgress(int percent, const QString &statusText);
    void SetAudioEndpointsProvider(AudioEndpointsProvider provider);
    void SetDiagnosticsProvider(DiagnosticsProvider provider);
    void SetLatencyDiagnosticsProvider(LatencyDiagnosticsProvider provider);
    void SetRadioControlProvider(RadioControlProvider provider);
    void SetUpdateCheckFeedback(Core::Update::CheckStatus status, const QString &version = {});

Q_SIGNALS:
    void BindDeviceRequested();
    void ManualUpdateCheckRequested();
    void LatencyProfileSwitchRequested(bool lowLatencySbc);
    void RadioServiceActionRequested(Gui::SettingsWindow::RadioServiceAction action);
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

private:
    using Refresher = std::function<void(const Core::Settings::Fields &)>;

    Core::QuickConnect::Controller &_quickConnect;
    AudioEndpointsProvider _audioEndpointsProvider;
    DiagnosticsProvider _diagnosticsProvider;
    LatencyDiagnosticsProvider _latencyDiagnosticsProvider;
    RadioControlProvider _radioControlProvider;
    std::vector<Refresher> _refreshers;
    // Rich text with colours written into it, rebuilt when the mode changes.
    std::vector<std::function<void()>> _themeRefreshers;

    Widget::Sidebar *_sidebar;
    QStackedWidget *_pages;
    std::array<int, 8> _pageIndex;

    // Controls with state that `Refresh` has to set by hand.
    Widget::Slider *_range{nullptr};
    QLabel *_rangeValue{nullptr};
    Widget::SettingsRow *_bindingRow{nullptr};
    Widget::PushButton *_bindButton{nullptr};
    Widget::PushButton *_unbind{nullptr};
    Widget::SettingsRow *_updateCheckRow{nullptr};
    Widget::PushButton *_checkUpdatesButton{nullptr};
    Widget::SettingsRow *_diagTransportRow{nullptr};
    Widget::SettingsRow *_diagConnectionRow{nullptr};
    Widget::PushButton *_copyDiagButton{nullptr};
    Widget::ComboBox *_preferredMic{nullptr};
    Widget::Toggle *_volumeCapToggle{nullptr};
    Widget::SettingsRow *_volumeCapSliderRow{nullptr};
    Widget::Slider *_volumeCapSlider{nullptr};
    QLabel *_volumeCapValue{nullptr};
    Widget::SettingsRow *_latencyProfileRow{nullptr};
    Widget::SegmentedControl *_latencyModeSegment{nullptr};

    // AirPods Control (Control Engine + live AAP settings)
    Widget::SettingsRow *_radioStatusRow{nullptr};
    QWidget *_radioProgressContainer{nullptr};
    Widget::ProgressBar *_radioProgressBar{nullptr};
    Widget::PushButton *_radioPrimaryButton{nullptr};
    RadioServiceAction _radioPrimaryAction{RadioServiceAction::InstallOrEnable};
    Widget::SettingsRow *_radioAdapterRow{nullptr};
    Widget::PushButton *_radioRestoreButton{nullptr};
    QWidget *_aapNoiseModeContainer{nullptr};
    Widget::AppleNoiseControl *_aapNoiseControl{nullptr};
    Widget::SettingsRow *_aapConvAwarenessRow{nullptr};
    Widget::Toggle *_aapConvAwareness{nullptr};
    Widget::SettingsRow *_aapPersonalizedVolRow{nullptr};
    Widget::Toggle *_aapPersonalizedVol{nullptr};
    Widget::SettingsRow *_aapOneBudAncRow{nullptr};
    Widget::Toggle *_aapOneBudAnc{nullptr};
    Widget::SettingsRow *_aapLoudSoundRow{nullptr};
    Widget::Toggle *_aapLoudSound{nullptr};
    Widget::SettingsRow *_aapHeadGesturesRow{nullptr};
    Widget::Toggle *_aapHeadGestures{nullptr};
    Widget::SettingsRow *_aapMicSideRow{nullptr};
    Widget::SegmentedControl *_aapMicSide{nullptr};
    Widget::SettingsRow *_aapPressSpeedRow{nullptr};
    Widget::SegmentedControl *_aapPressSpeed{nullptr};
    Widget::SettingsRow *_aapPressHoldRow{nullptr};
    Widget::SegmentedControl *_aapPressHold{nullptr};
    Widget::SettingsRow *_aapToneVolumeRow{nullptr};
    Widget::Slider *_aapToneVolumeSlider{nullptr};
    QLabel *_aapToneVolumeValue{nullptr};
    Widget::SettingsRow *_aapAllowOffRow{nullptr};
    Widget::Toggle *_aapAllowOff{nullptr};
    Widget::SettingsRow *_aapHearingAidRow{nullptr};
    Widget::Toggle *_aapHearingAid{nullptr};
    Widget::SettingsRow *_aapMediaAssistRow{nullptr};
    Widget::Toggle *_aapMediaAssist{nullptr};
    Widget::SettingsRow *_aapHeartRateRow{nullptr};
    Widget::Toggle *_aapHeartRate{nullptr};
    Widget::SettingsRow *_aapHeartRateTelemetryRow{nullptr};
    Widget::HeartRateCard *_aapHeartRateCard{nullptr};

    Widget::Toggle *_earDetection{nullptr};
    Widget::SettingsRow *_earDetectionResumeRow{nullptr};
    Widget::SettingsRow *_earDetectionSingleBudRow{nullptr};
    Widget::Toggle *_quickConnectEnabled{nullptr};
    Widget::SettingsRow *_quickConnectDeviceRow{nullptr};
    Widget::ComboBox *_quickConnectDevice{nullptr};
#if defined APB_DEBUG
    Widget::Toggle *_advOverride{nullptr};
    QTextEdit *_advOverrideText{nullptr};
#endif

    QWidget *BuildGeneralPage();
    QWidget *BuildDisplayPage();
    QWidget *BuildControlPage();
    QWidget *BuildHealthPage();
    QWidget *BuildAudioPage();
    QWidget *BuildFeaturesPage();
    QWidget *BuildAboutPage();
    QWidget *BuildDebugPage();
    QWidget *WrapPage(const QString &title, QWidget *content);

    void Refresh();
    void RefreshBinding(const Core::Settings::Fields &fields);
    void RefreshPreferredMicrophones(const Core::Settings::Fields &fields);
    void RefreshQuickConnectDevices(const Core::Settings::Fields &fields);
    void RestoreDefaults();
    void UpdateAdvOverride();

    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};

} // namespace Gui
