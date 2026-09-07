// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/TrayIcon.h"

#include "Source/Presentation/Theme.h"

#include <QFont>
#include <QApplication>
#include <QPainter>
#include <QSvgRenderer>

#include <Config.h>
namespace Gui {

TrayIcon::TrayIcon(Core::QuickConnect::Controller &quickConnect)
    : _quickConnect{quickConnect}, _settingsWindow{quickConnect}
{
    _singleClickTimer.setSingleShot(true);
    connect(&_singleClickTimer, &QTimer::timeout, this, [this] {
        ExecuteTrayActivation(_trayActivationState.OnSingleClickTimeout());
    });

    connect(_actionNewVersion, &QAction::triggered, this, &TrayIcon::OnNewVersionClicked);
    connect(_actionSettings, &QAction::triggered, this, &TrayIcon::OnSettingsClicked);
    connect(_actionAbout, &QAction::triggered, this, &TrayIcon::OnAboutClicked);
    connect(_actionQuit, &QAction::triggered, qApp, &QApplication::quit, Qt::QueuedConnection);
    connect(_tray, &QSystemTrayIcon::activated, this, &TrayIcon::OnIconClicked);
    connect(_tray, &QSystemTrayIcon::messageClicked, this, &TrayIcon::ShowMainWindowRequested);
    connect(
        &_quickConnect, &Core::QuickConnect::Controller::OutcomeChanged, this,
        &TrayIcon::OnQuickConnectOutcome);

    connect(
        this, &TrayIcon::OnTrayIconBatteryChangedSafely, this, &TrayIcon::OnTrayIconBatteryChanged);
    connect(
        this, &TrayIcon::OnLowBatteryThresholdChangedSafely, this,
        &TrayIcon::OnLowBatteryThresholdChanged);

    _actionNewVersion->setVisible(false);

    _menu->addAction(_actionNewVersion);
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

void TrayIcon::VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo)
{
    _updateReleaseInfo = releaseInfo;
    _actionNewVersion->setVisible(true);
    Repaint();
}

void TrayIcon::ShowMainWindow()
{
    emit ShowMainWindowRequested();
}

void TrayIcon::ShowContextMenu(const QPoint &position)
{
    _menu->popup(position);
}

void TrayIcon::Repaint()
{
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

    const auto toolTip = "AirpodsBridge\n" + toolTipContent.trimmed();
    _tray->setToolTip(toolTip);
    emit ToolTipChanged(toolTip);

    // RepaintIcon

    std::optional<QString> iconText;

    do {
        if (minBattery.Available()) {
            const auto drawBattery = [&] {
                switch (_trayIconBatteryBehavior) {
                case Core::Settings::TrayIconBatteryBehavior::Disable:
                    return false;
                case Core::Settings::TrayIconBatteryBehavior::WhenLowBattery:
                    return minBattery.IsLowBattery(_lowBatteryThreshold);
                case Core::Settings::TrayIconBatteryBehavior::Always:
                    return true;
                default:
                    return false;
                }
            }();

            if (drawBattery) {
                iconText = QString::number(minBattery.Value());
                break;
            }
        }

        iconText.reset();
    } while (false);

    static const QColor kNewVersionAvailableDot = Qt::yellow;
    const IconRenderState nextIconRenderState{
        .text = iconText,
        .showUpdateDot = _updateReleaseInfo.has_value(),
    };

    if (_lastIconRenderState == nextIconRenderState) {
        return;
    }

    auto optIcon = GenerateIcon(
        64, nextIconRenderState.text,
        nextIconRenderState.showUpdateDot ? std::optional<QColor>{kNewVersionAvailableDot}
                                          : std::nullopt);
    if (optIcon.has_value()) {
        _tray->setIcon(QIcon{QPixmap::fromImage(optIcon.value())});
        _lastIconRenderState = nextIconRenderState;
    }
}

std::optional<QImage> TrayIcon::GenerateIcon(
    int size, const std::optional<QString> &optText, const std::optional<QColor> &dot)
{
    QImage result{size, size, QImage::Format_ARGB32};
    QPainter painter{&result};

    result.fill(Qt::transparent);

    QSvgRenderer{QString{Config::QrcIconSvg}}.render(&painter);

    painter.setRenderHint(QPainter::Antialiasing);

    painter.save();
    do {
        if (!optText.has_value() || optText->isEmpty()) {
            break;
        }
        const auto &text = optText.value();

        static std::unordered_map<int, std::optional<QFont>> trayIconFonts;

        const auto &adjustFont = [](const QString &family,
                                    int desiredSize) -> std::optional<QFont> {
            int lastHeight = 0;

            for (int i = 1; i < 100; i++) {
                QFont font{family, i};
                font.setBold(true);

                int currentHeight = QFontMetrics{font}.height();
                if (currentHeight == desiredSize ||
                    lastHeight < desiredSize && currentHeight > desiredSize) [[unlikely]]
                {
                    LOG(Info,
                        "Found a suitable font for the tray icon. "
                        "Family: '{}', desiredSize: '{}', fontHeight: '{}', fontSize: '{}'",
                        family, desiredSize, currentHeight, i);
                    return font;
                }
                lastHeight = currentHeight;
            }

            LOG(Warn,
                "Cannot find a suitable font for the tray icon. Family: '{}', desiredSize: "
                "'{}'",
                family, desiredSize);

            return std::nullopt;
        };

        auto textHeight = size * 0.8;

        if (!trayIconFonts.contains(textHeight)) {
            trayIconFonts[textHeight] = adjustFont(qApp->font().family(), textHeight);
        }

        const auto &optFont = trayIconFonts[textHeight];
        if (!optFont.has_value()) {
            break;
        }
        const auto &font = optFont.value();
        const auto &fontMetrics = QFontMetrics{font};

        const auto textWidth = fontMetrics.width(text);
        textHeight = fontMetrics.height();

        constexpr auto kMargin = QSizeF{2, 0};

        const auto textRect = QRectF{
            (double)size - textWidth - kMargin.width(), size - textHeight - kMargin.height(),
            (double)textWidth, textHeight};
        const auto bgRect = QRectF{
            textRect.left() - kMargin.width(), textRect.top() - kMargin.height(),
            textRect.width() + kMargin.width() * 2, textRect.height() + kMargin.height() * 2};

        // The tray is Windows', not ours: the badge is one red in every mode, and the
        // icon is not re-rendered when the mode changes.
        painter.setPen(Qt::white);
        painter.setBrush(Theme::Light().BatteryLow);
        painter.setFont(font);

        painter.drawRoundedRect(bgRect, 10, 10);
        painter.drawText(textRect, text);

    } while (false);
    painter.restore();

    painter.save();
    do {
        if (!dot.has_value()) {
            break;
        }

        const double dotDiameter = size * 0.4;

        painter.setBrush(dot.value());
        painter.drawEllipse(QRectF{size - dotDiameter, 0, dotDiameter, dotDiameter});
    } while (false);
    painter.restore();

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
    QString text;
    QSystemTrayIcon::MessageIcon icon = QSystemTrayIcon::Information;

    switch (outcome) {
    case Core::QuickConnect::Outcome::Disabled:
        text = tr("Quick connection is disabled.");
        break;
    case Core::QuickConnect::Outcome::NoDevice:
        text = tr("Choose a Bluetooth audio device in Settings first.");
        icon = QSystemTrayIcon::Warning;
        break;
    case Core::QuickConnect::Outcome::DeviceUnavailable:
        text = tr("The selected device is not available. Turn it on or bring it into range.");
        icon = QSystemTrayIcon::Warning;
        break;
    case Core::QuickConnect::Outcome::AlreadyConnected:
        text = tr("%1 is already connected.").arg(deviceName);
        break;
    case Core::QuickConnect::Outcome::RequestStarted:
        text = tr("Connecting to %1...").arg(deviceName);
        break;
    case Core::QuickConnect::Outcome::Connected:
        text = tr("%1 connected.").arg(deviceName);
        break;
    case Core::QuickConnect::Outcome::TimedOut:
        text = tr("Connection to %1 timed out.").arg(deviceName);
        icon = QSystemTrayIcon::Warning;
        break;
    case Core::QuickConnect::Outcome::Failed:
        text = tr("Windows could not connect to %1.").arg(deviceName);
        icon = QSystemTrayIcon::Warning;
        break;
    default:
        return;
    }

    ShowMessage(tr("Quick connect"), text, icon);
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
