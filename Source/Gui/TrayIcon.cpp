// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/TrayIcon.h"

#include <array>

#include <QApplication>
#include <QFont>
#include <QPainter>
#include <QSvgRenderer>

#include <Config.h>
#include "Source/Presentation/Theme.h"

namespace Gui {
namespace {

// Windows NOTIFYICONDATA.szTip holds 128 wchar_t including the NUL terminator.
constexpr int kWindowsTrayToolTipLimit = 127;

QString ClampTrayToolTip(const QString &text)
{
    if (text.size() <= kWindowsTrayToolTipLimit) {
        return text;
    }
    return text.left(kWindowsTrayToolTipLimit - 1) + QChar{0x2026};
}

} // namespace

TrayIcon::TrayIcon(Core::QuickConnect::Controller &quickConnect)
    : _quickConnect{quickConnect}, _settingsWindow{quickConnect}
{
    _singleClickTimer.setSingleShot(true);
    connect(&_singleClickTimer, &QTimer::timeout, this, [this] {
        ExecuteTrayActivation(_trayActivationState.OnSingleClickTimeout());
    });

    connect(_actionNewVersion, &QAction::triggered, this, &TrayIcon::OnNewVersionClicked);
    connect(
        _actionQuickConnect, &QAction::triggered, this, [this] { _quickConnect.RequestToggle(); });
    connect(_actionSwitchAudio, &QAction::triggered, this, [this] {
        emit SwitchAudioOutputRequested();
    });
    connect(_actionSettings, &QAction::triggered, this, &TrayIcon::OnSettingsClicked);
    connect(_actionAbout, &QAction::triggered, this, &TrayIcon::OnAboutClicked);
    connect(_actionQuit, &QAction::triggered, qApp, &QApplication::quit, Qt::QueuedConnection);
    connect(_tray, &QSystemTrayIcon::activated, this, &TrayIcon::OnIconClicked);
    connect(_tray, &QSystemTrayIcon::messageClicked, this, &TrayIcon::ShowMainWindowRequested);
    connect(
        &_quickConnect, &Core::QuickConnect::Controller::OutcomeChanged, this,
        &TrayIcon::OnQuickConnectOutcome);
    connect(
        &_quickConnect, &Core::QuickConnect::Controller::DevicesChanged, this,
        &TrayIcon::RefreshQuickConnectMenu);

    connect(
        &_settingsWindow, &SettingsWindow::BindDeviceRequested, this,
        &TrayIcon::BindDeviceRequested);
    connect(
        &_settingsWindow, &SettingsWindow::ManualUpdateCheckRequested, this,
        &TrayIcon::ManualUpdateCheckRequested);
    connect(
        &_settingsWindow, &SettingsWindow::LatencyProfileSwitchRequested, this,
        &TrayIcon::LatencyProfileSwitchRequested);
    connect(
        &_settingsWindow, &SettingsWindow::RadioServiceActionRequested, this,
        &TrayIcon::RadioServiceActionRequested);
    connect(
        &_settingsWindow, &SettingsWindow::NoiseControlModeRequested, this,
        &TrayIcon::NoiseControlModeRequested);
    connect(
        &_settingsWindow, &SettingsWindow::ConversationalAwarenessRequested, this,
        &TrayIcon::ConversationalAwarenessRequested);
    connect(
        &_settingsWindow, &SettingsWindow::AdaptiveNoiseLevelRequested, this,
        &TrayIcon::AdaptiveNoiseLevelRequested);
    connect(
        &_settingsWindow, &SettingsWindow::OneBudAncRequested, this, &TrayIcon::OneBudAncRequested);
    connect(
        &_settingsWindow, &SettingsWindow::PersonalizedVolumeRequested, this,
        &TrayIcon::PersonalizedVolumeRequested);
    connect(
        &_settingsWindow, &SettingsWindow::LoudSoundReductionRequested, this,
        &TrayIcon::LoudSoundReductionRequested);
    connect(
        &_settingsWindow, &SettingsWindow::HeadGesturesRequested, this,
        &TrayIcon::HeadGesturesRequested);
    connect(
        &_settingsWindow, &SettingsWindow::MicrophoneSideRequested, this,
        &TrayIcon::MicrophoneSideRequested);
    connect(
        &_settingsWindow, &SettingsWindow::PressSpeedRequested, this,
        &TrayIcon::PressSpeedRequested);
    connect(
        &_settingsWindow, &SettingsWindow::PressHoldDurationRequested, this,
        &TrayIcon::PressHoldDurationRequested);
    connect(
        &_settingsWindow, &SettingsWindow::ToneVolumeRequested, this,
        &TrayIcon::ToneVolumeRequested);
    connect(
        &_settingsWindow, &SettingsWindow::AllowOffListeningModeRequested, this,
        &TrayIcon::AllowOffListeningModeRequested);
    connect(
        &_settingsWindow, &SettingsWindow::HearingAidRequested, this,
        &TrayIcon::HearingAidRequested);
    connect(
        &_settingsWindow, &SettingsWindow::MediaAssistRequested, this,
        &TrayIcon::MediaAssistRequested);
    connect(
        &_settingsWindow, &SettingsWindow::HeartRateMonitoringRequested, this,
        &TrayIcon::HeartRateMonitoringRequested);

    connect(
        this, &TrayIcon::OnTrayIconBatteryChangedSafely, this, &TrayIcon::OnTrayIconBatteryChanged);
    connect(
        this, &TrayIcon::OnLowBatteryThresholdChangedSafely, this,
        &TrayIcon::OnLowBatteryThresholdChanged);

    // Build the AAP Noise Control submenu (hidden until an AAP control transport connects).
    _noiseControlGroup->setExclusive(true);
    const auto addNoiseAction = [&](const QString &label, Core::AirPods::NoiseControlMode mode) {
        auto *action = _noiseControlMenu->addAction(label);
        action->setCheckable(true);
        _noiseControlGroup->addAction(action);
        connect(action, &QAction::triggered, this, [this, mode] {
            emit NoiseControlModeRequested(mode);
        });
        return action;
    };
    _actionNoiseOff = addNoiseAction(tr("Off"), Core::AirPods::NoiseControlMode::Off);
    _actionNoiseAnc = addNoiseAction(
        tr("Noise Cancellation"), Core::AirPods::NoiseControlMode::NoiseCancellation);
    _actionNoiseTransparency =
        addNoiseAction(tr("Transparency"), Core::AirPods::NoiseControlMode::Transparency);
    _actionNoiseAdaptive =
        addNoiseAction(tr("Adaptive"), Core::AirPods::NoiseControlMode::Adaptive);

    _noiseControlMenu->addSeparator();
    _actionConversationalAwareness = _noiseControlMenu->addAction(tr("Conversation Awareness"));
    _actionConversationalAwareness->setCheckable(true);
    connect(_actionConversationalAwareness, &QAction::triggered, this, [this](bool checked) {
        emit ConversationalAwarenessRequested(checked);
    });

    _actionNewVersion->setVisible(false);
    _actionQuickConnect->setVisible(false);
    _actionSwitchAudio->setVisible(false);
    _noiseControlMenu->menuAction()->setVisible(false);

    _menu->addAction(_actionNewVersion);
    _menu->addAction(_actionQuickConnect);
    _menu->addAction(_actionSwitchAudio);
    _menu->addMenu(_noiseControlMenu);
    _menu->addSeparator();
    _menu->addAction(_actionSettings);
    _menu->addSeparator();
    _menu->addAction(_actionAbout);
    _menu->addAction(_actionQuit);

    _tray->setContextMenu(_menu);
    _tray->setIcon(qApp->windowIcon());
    _tray->show();
}

void TrayIcon::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _airPodsState = state;
    if (_settingsWindow.isVisible()) {
        _settingsWindow.RefreshRadioControl();
    }
    Repaint();
}

void TrayIcon::Unavailable()
{
    _status = Status::Unavailable;
    _airPodsState.reset();
    Repaint();
}

void TrayIcon::Disconnect()
{
    // No device is bound yet, so "Waiting for Binding" outranks "Disconnected". The scanner
    // reports itself available on every (re)start, and that must not overwrite this state.
    if (_status == Status::Unbind) {
        return;
    }

    _status = Status::Disconnected;
    _airPodsState.reset();
    Repaint();
}

void TrayIcon::Unbind()
{
    _status = Status::Unbind;
    _airPodsState.reset();
    Repaint();
}

void TrayIcon::SetDeviceConnected(bool connected)
{
    if (_deviceConnected == connected) {
        return;
    }
    _deviceConnected = connected;
    UpdateDynamicMenus();
}

void TrayIcon::VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo)
{
    _updateReleaseInfo = releaseInfo;
    _actionNewVersion->setVisible(true);
    Repaint();
}

void TrayIcon::SetUpdateCheckFeedback(Core::Update::CheckStatus status, const QString &version)
{
    _settingsWindow.SetUpdateCheckFeedback(status, version);
}

void TrayIcon::RefreshQuickConnectMenu()
{
    UpdateDynamicMenus();
}

void TrayIcon::RefreshSettings()
{
    _settingsWindow.RefreshBindingState();
}

void TrayIcon::OpenSettings(SettingsWindow::Section section)
{
    _settingsWindow.SelectSection(section);
    _settingsWindow.show();
    _settingsWindow.raise();
    _settingsWindow.activateWindow();
}

void TrayIcon::SetAudioEndpointsProvider(SettingsWindow::AudioEndpointsProvider provider)
{
    _settingsWindow.SetAudioEndpointsProvider(std::move(provider));
}

void TrayIcon::SetDiagnosticsProvider(SettingsWindow::DiagnosticsProvider provider)
{
    _settingsWindow.SetDiagnosticsProvider(std::move(provider));
}

void TrayIcon::SetLatencyDiagnosticsProvider(SettingsWindow::LatencyDiagnosticsProvider provider)
{
    _settingsWindow.SetLatencyDiagnosticsProvider(std::move(provider));
}

void TrayIcon::SetRadioControlProvider(SettingsWindow::RadioControlProvider provider)
{
    _settingsWindow.SetRadioControlProvider(std::move(provider));
}

void TrayIcon::NotifyAudioEndpointsChanged()
{
    _settingsWindow.RefreshAudioDevices();
}

void TrayIcon::NotifyLatencyDiagnosticsChanged()
{
    _settingsWindow.RefreshLatencyDiagnostics();
    _settingsWindow.RefreshDiagnostics();
}

void TrayIcon::NotifyRadioControlChanged()
{
    _settingsWindow.RefreshRadioControl();
    _settingsWindow.RefreshDiagnostics();
}

void TrayIcon::SetRadioSetupBusy(bool busy, const QString &statusText)
{
    _settingsWindow.SetRadioSetupBusy(busy, statusText);
    _settingsWindow.RefreshDiagnostics();
}

void TrayIcon::SetRadioSetupProgress(int percent, const QString &statusText)
{
    _settingsWindow.SetRadioSetupProgress(percent, statusText);
}

void TrayIcon::ShowMainWindow()
{
    emit ShowMainWindowRequested();
}

void TrayIcon::ShowContextMenu(const QPoint &position)
{
    UpdateDynamicMenus();
    _menu->popup(position);
}

void TrayIcon::UpdateDynamicMenus()
{
    const bool hasQuickConnectDevice = _quickConnect.HasDevice();
    _actionQuickConnect->setVisible(hasQuickConnectDevice);
    if (hasQuickConnectDevice) {
        QString deviceName = _quickConnect.DeviceNameFor(_quickConnect.DeviceId());
        if (deviceName.isEmpty() && _airPodsState.has_value()) {
            deviceName = _airPodsState->displayName;
        }
        if (deviceName.isEmpty()) {
            deviceName = tr("AirPods");
        }
        _actionQuickConnect->setText(
            _deviceConnected ? tr("Disconnect %1").arg(deviceName)
                             : tr("Connect %1").arg(deviceName));
    }
    _actionSwitchAudio->setVisible(_deviceConnected);

    bool showNoiseMenu = false;
    if (_airPodsState.has_value()) {
        const auto &state = _airPodsState.value();
        const auto caps = Core::AirPods::CapabilitiesFor(state.model);
        if (state.noiseControl.controllable && caps.SupportsNoiseControl()) {
            showNoiseMenu = true;
            _actionNoiseAdaptive->setVisible(caps.supportsAdaptiveAudio);

            const auto mode =
                state.noiseControl.activeMode.value_or(Core::AirPods::NoiseControlMode::Off);
            _actionNoiseOff->setChecked(mode == Core::AirPods::NoiseControlMode::Off);
            _actionNoiseAnc->setChecked(mode == Core::AirPods::NoiseControlMode::NoiseCancellation);
            _actionNoiseTransparency->setChecked(
                mode == Core::AirPods::NoiseControlMode::Transparency);
            _actionNoiseAdaptive->setChecked(mode == Core::AirPods::NoiseControlMode::Adaptive);

            _actionConversationalAwareness->setVisible(caps.supportsConversationalAwareness);
            _actionConversationalAwareness->setChecked(
                state.noiseControl.conversationalAwareness.value_or(false));
        }
    }
    _noiseControlMenu->menuAction()->setVisible(showNoiseMenu);
}

void TrayIcon::Repaint()
{
    UpdateDynamicMenus();

    QString toolTipContent;
    Core::AirPods::Battery minBattery;

    switch (_status) {
    case Status::Unavailable:
    case Status::Disconnected:
    case Status::Unbind:
        toolTipContent = DisplayableStatus(_status);
        break;
    case Status::Updating: {
        if (!_airPodsState.has_value()) {
            break;
        }
        const auto &state = _airPodsState.value();

        toolTipContent += state.displayName;
        if (state.audioProfile == Core::AirPods::AudioProfile::HandsFree) {
            toolTipContent += QStringLiteral(" (%1)").arg(tr("Hands-Free"));
        }

        const auto strLeft{tr("Left")}, strRight{tr("Right")}, strCase{tr("Case")},
            strCharging{tr("charging")};

        const auto textCharging = QString{" (%1)"}.arg(strCharging),
                   textPlaceHolder = QString{"\n%1: %2%%3"};

        // One thing, one line: no left, right or case.
        if (Core::AirPods::IsOnePiece(state.model)) {
            const auto piece = Core::AirPods::OnePieceState(state.pods);
            if (piece.battery.Available()) {
                toolTipContent += textPlaceHolder.arg(tr("Battery"))
                                      .arg(piece.battery.Value())
                                      .arg(piece.isCharging ? textCharging : QString{});
                minBattery = piece.battery.Value();
            }
            break;
        }

        // clang-format off
        if (state.pods.left.battery.Available()) {
            const auto batteryValue = state.pods.left.battery.Value();

            toolTipContent += textPlaceHolder
                .arg(strLeft)
                .arg(batteryValue)
                .arg(state.pods.left.isCharging ? textCharging : QString{});

            minBattery = batteryValue;
        }

        if (state.pods.right.battery.Available()) {
            const auto batteryValue = state.pods.right.battery.Value();

            toolTipContent += textPlaceHolder
                .arg(strRight)
                .arg(batteryValue)
                .arg(state.pods.right.isCharging ? textCharging : QString{});

            if (minBattery.Available() && batteryValue < minBattery.Value() ||
                !minBattery.Available()) {
                minBattery = batteryValue;
            }
        }

        if (state.caseBox.battery.Available()) {
            toolTipContent += textPlaceHolder
                .arg(strCase)
                .arg(state.caseBox.battery.Value())
                .arg(state.caseBox.isCharging ? textCharging : QString{});
        }
        // clang-format on
        break;
    }
    default:
        APB_ASSERT(false);
    }

    if (_updateReleaseInfo.has_value()) {
        toolTipContent += '\n' + _actionNewVersion->text();
    }

    const auto toolTip = ClampTrayToolTip("AirpodsBridge\n" + toolTipContent.trimmed());
    _tray->setToolTip(toolTip);
    emit ToolTipChanged(toolTip);

    // RepaintIcon

    std::optional<QString> iconText;
    bool isLow = false;

    if (minBattery.Available()) {
        isLow = minBattery.IsLowBattery(_lowBatteryThreshold);
        const auto drawBattery = [&] {
            switch (_trayIconBatteryBehavior) {
            case Core::Settings::TrayIconBatteryBehavior::Disable:
                return false;
            case Core::Settings::TrayIconBatteryBehavior::WhenLowBattery:
                return isLow;
            case Core::Settings::TrayIconBatteryBehavior::Always:
                return true;
            default:
                return false;
            }
        }();

        if (drawBattery) {
            iconText = QString::number(minBattery.Value());
        }
    }

    const IconRenderState nextIconRenderState{
        .text = iconText,
        .isLow = isLow,
        .showUpdateDot = _updateReleaseInfo.has_value(),
    };

    if (_lastIconRenderState == nextIconRenderState) {
        return;
    }

    const auto dotColor = nextIconRenderState.showUpdateDot
                              ? std::optional<QColor>{Theme::Light().Accent}
                              : std::nullopt;
    _tray->setIcon(
        GenerateMultiSizeIcon(nextIconRenderState.text, nextIconRenderState.isLow, dotColor));
    _lastIconRenderState = nextIconRenderState;
}

QIcon TrayIcon::GenerateMultiSizeIcon(
    const std::optional<QString> &optText, bool isLow, const std::optional<QColor> &dot)
{
    // Render crisp exact-pixel sizes covering 100%, 125%, 150%, 200%, and high-DPI trays
    // instead of letting Windows blur a single 64px bitmap down to 16px.
    QIcon icon;
    constexpr std::array<int, 5> kSizes{16, 20, 24, 32, 64};
    for (const int size : kSizes) {
        icon.addPixmap(QPixmap::fromImage(GenerateIcon(size, optText, isLow, dot)));
    }
    return icon;
}

QImage TrayIcon::GenerateIcon(
    int size, const std::optional<QString> &optText, bool isLow, const std::optional<QColor> &dot)
{
    QImage result{size, size, QImage::Format_ARGB32_Premultiplied};
    result.fill(Qt::transparent);

    QPainter painter{&result};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    QSvgRenderer{QString{Config::QrcIconSvg}}.render(&painter);

    if (optText.has_value() && !optText->isEmpty()) {
        painter.save();
        const auto &text = optText.value();

        QFont font = Theme::Font::Body();
        font.setBold(true);
        const int targetPixelSize = std::max(8, static_cast<int>(std::lround(size * 0.56)));
        font.setPixelSize(targetPixelSize);

        QFontMetrics metrics{font};
        while (font.pixelSize() > 7 && metrics.horizontalAdvance(text) > size - 2) {
            font.setPixelSize(font.pixelSize() - 1);
            metrics = QFontMetrics{font};
        }

        const qreal padX = std::max(1.5, size * 0.06);
        const qreal badgeW = std::min<qreal>(size, metrics.horizontalAdvance(text) + padX * 2.0);
        const qreal badgeH = std::min<qreal>(size, metrics.ascent() + std::max(2.0, size * 0.14));
        const QRectF bgRect{size - badgeW, size - badgeH, badgeW, badgeH};
        const qreal cornerRadius = std::min(bgRect.height() * 0.35, size * 0.18);

        // Semantic colour: Apple green when healthy, red only when low.
        const QColor badgeColor = isLow ? Theme::Light().BatteryLow : Theme::Light().BatteryGood;
        painter.setPen(Qt::NoPen);
        painter.setBrush(badgeColor);
        painter.drawRoundedRect(bgRect, cornerRadius, cornerRadius);

        painter.setPen(Qt::white);
        painter.setFont(font);
        painter.drawText(bgRect, Qt::AlignCenter, text);
        painter.restore();
    }

    if (dot.has_value()) {
        painter.save();
        const qreal dotDiameter = std::max(4.0, size * 0.32);
        painter.setPen(Qt::NoPen);
        painter.setBrush(dot.value());
        painter.drawEllipse(QRectF{size - dotDiameter - 0.5, 0.5, dotDiameter, dotDiameter});
        painter.restore();
    }

    return result;
}

void TrayIcon::OnNewVersionClicked()
{
    APB_ASSERT(_updateReleaseInfo.has_value());

    _actionNewVersion->setVisible(false);

    auto releaseInfo = std::move(_updateReleaseInfo.value());
    _updateReleaseInfo.reset();
    Repaint();

    emit UserUpdateRequested(releaseInfo);
}

void TrayIcon::OnSettingsClicked()
{
    // Reopening lands on the first page. A window the user already has open on some
    // other settings page is left where it is.
    if (!_settingsWindow.isVisible() ||
        _settingsWindow.CurrentSection() == SettingsWindow::Section::About)
    {
        _settingsWindow.SelectSection(SettingsWindow::Section::General);
    }

    _settingsWindow.show();
    _settingsWindow.raise();
    _settingsWindow.activateWindow();
}

void TrayIcon::OnAboutClicked()
{
    _settingsWindow.SelectSection(SettingsWindow::Section::About);
    _settingsWindow.show();
    _settingsWindow.raise();
    _settingsWindow.activateWindow();
}

void TrayIcon::OnIconClicked(QSystemTrayIcon::ActivationReason reason)
{
    const auto result = _trayActivationState.OnActivation(reason, _quickConnect.IsEnabled());

    switch (result.timer) {
    case SingleClickTimer::Start:
        // Read the interval each time; the user can change it in Windows while we are running.
        _singleClickTimer.start(QApplication::doubleClickInterval());
        break;
    case SingleClickTimer::Stop:
        _singleClickTimer.stop();
        break;
    case SingleClickTimer::Unchanged:
        break;
    }

    ExecuteTrayActivation(result.action);
}

void TrayIcon::ExecuteTrayActivation(TrayActivationAction action)
{
    if (action == TrayActivationAction::QuickConnect) {
        _quickConnect.Request();
        return;
    }

    if (action == TrayActivationAction::ShowMainWindow) {
        ShowMainWindow();
    }
}

void TrayIcon::OnQuickConnectOutcome(Core::QuickConnect::Outcome outcome, const QString &deviceName)
{
    Q_UNUSED(deviceName);

    // Do not spam Windows balloon/toast notifications over the popup for routine connect,
    // disconnect, or timeout outcomes -- the popup itself shows `"Connecting..."`, live
    // battery/controls, and subtle inline status messages. Only notify when the user has not
    // configured a Quick Connect device yet.
    if (outcome == Core::QuickConnect::Outcome::NoDevice) {
        ShowMessage(
            tr("Quick connect"), tr("Choose a Bluetooth audio device in Settings first."),
            QSystemTrayIcon::Warning);
    }
}

void TrayIcon::OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior value)
{
    _trayIconBatteryBehavior = value;
    Repaint();
}

void TrayIcon::OnLowBatteryThresholdChanged(uint32_t threshold)
{
    _lowBatteryThreshold = threshold;
    Repaint();
}

} // namespace Gui
