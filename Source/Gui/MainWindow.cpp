// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/MainWindow.h"

#include <QCloseEvent>
#include <QCursor>
#include <QFontMetrics>
#include <QLinearGradient>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>
#include <QSignalBlocker>

#include <Config.h>
#include "Source/Core/Settings.h"
#include "Source/Core/WindowEffects.h"
#include "Source/Domain/AppleCP.h"
#include "Source/Gui/DownloadWindow.h"
#include "Source/Gui/Painting.h"
#include "Source/Gui/SelectWindow.h"
#include "Source/Gui/Widget/AppleNoiseControl.h"
#include "Source/Gui/Widget/HeartRateCard.h"
#include "Source/Gui/Widget/Label.h"
#include "Source/Presentation/DeviceArtwork.h"
#include "Source/Presentation/Theme.h"
#include "Source/Support/Error.h"
#include "Source/Support/Helper.h"

using namespace std::chrono_literals;

namespace Gui {

class CloseButton : public QWidget
{
    Q_OBJECT

public:
    CloseButton(QWidget *parent = nullptr) : QWidget{parent}
    {
        // Concentric with the card's 28 px squircle at 14 px inset (28 - 14 = 14 px radius).
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(tr("Close"));
    }

Q_SIGNALS:
    void Clicked();

private:
    bool _isHovering{false}, _isHoldDown{false};

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter{this};
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

        DrawBackground(painter);
        DrawX(painter);
    }

    void enterEvent(QEvent *event) override
    {
        Q_UNUSED(event);
        _isHovering = true;
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        Q_UNUSED(event);
        _isHovering = false;
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event);
        _isHoldDown = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event);
        _isHoldDown = false;
        Q_EMIT Clicked();
        update();
    }

    void DrawBackground(QPainter &painter)
    {
        painter.save();
        {
            const auto &colours = Theme::Colours();
            const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;
            QColor color = colours.Control;
            if (_isHoldDown) {
                color = colours.ControlPressed;
            }
            else if (_isHovering) {
                color = colours.ControlHover;
            }

            const QRectF circle = QRectF{rect()}.adjusted(0.75, 0.75, -0.75, -0.75);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush{color});
            painter.drawEllipse(circle);

            QLinearGradient rim{circle.topLeft(), circle.bottomRight()};
            if (isDark) {
                rim.setColorAt(0.0, QColor{255, 255, 255, 86});
                rim.setColorAt(0.5, QColor{255, 255, 255, 16});
                rim.setColorAt(1.0, QColor{255, 255, 255, 48});
            }
            else {
                rim.setColorAt(0.0, QColor{255, 255, 255, 210});
                rim.setColorAt(1.0, QColor{0, 0, 0, 24});
            }
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{QBrush{rim}, 1.0});
            painter.drawEllipse(circle);
        }
        painter.restore();
    }

    void DrawX(QPainter &painter)
    {
        painter.save();
        {
            painter.setPen(QPen{
                Theme::Colours().Glyph, Theme::Metric::CloseButtonGlyphWidth, Qt::SolidLine,
                Qt::RoundCap});
            painter.setBrush(Qt::NoBrush);

            const QSize size = this->size();
            const qreal margin = size.width() * Theme::Metric::CloseButtonGlyphInset;

            painter.drawLine(
                QPointF{margin, margin}, QPointF{size.width() - margin, size.height() - margin});

            painter.drawLine(
                QPointF{size.width() - margin, margin}, QPointF{margin, size.height() - margin});
        }
        painter.restore();
    }
};

class ControlsButton : public QWidget
{
    Q_OBJECT

public:
    explicit ControlsButton(QWidget *parent = nullptr) : QWidget{parent}
    {
        setFixedSize(28, 28);
        setCursor(Qt::PointingHandCursor);
        setAccessibleName(tr("AirPods controls and settings"));
        setToolTip(tr("AirPods controls & settings"));
    }

Q_SIGNALS:
    void Clicked();

private:
    bool _isHovering{false};
    bool _isHoldDown{false};

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing, true);

        const auto &colours = Theme::Colours();
        const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;
        QColor bg = colours.Control;
        if (_isHoldDown) {
            bg = colours.ControlPressed;
        }
        else if (_isHovering) {
            bg = colours.ControlHover;
        }

        const QRectF circle = QRectF{rect()}.adjusted(0.75, 0.75, -0.75, -0.75);
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawEllipse(circle);

        QLinearGradient rim{circle.topLeft(), circle.bottomRight()};
        if (isDark) {
            rim.setColorAt(0.0, QColor{255, 255, 255, 86});
            rim.setColorAt(0.5, QColor{255, 255, 255, 16});
            rim.setColorAt(1.0, QColor{255, 255, 255, 48});
        }
        else {
            rim.setColorAt(0.0, QColor{255, 255, 255, 210});
            rim.setColorAt(1.0, QColor{0, 0, 0, 24});
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{QBrush{rim}, 1.0});
        painter.drawEllipse(circle);

        const QPointF c = QRectF{rect()}.center();
        painter.setPen(QPen{colours.Glyph, 1.65, Qt::SolidLine, Qt::RoundCap});
        painter.drawLine(QPointF{c.x() - 5.2, c.y() - 2.8}, QPointF{c.x() + 5.2, c.y() - 2.8});
        painter.drawLine(QPointF{c.x() - 5.2, c.y() + 2.8}, QPointF{c.x() + 5.2, c.y() + 2.8});

        painter.setPen(Qt::NoPen);
        painter.setBrush(colours.Glyph);
        painter.drawEllipse(QPointF{c.x() - 1.8, c.y() - 2.8}, 2.0, 2.0);
        painter.drawEllipse(QPointF{c.x() + 2.0, c.y() + 2.8}, 2.0, 2.0);
    }

    void enterEvent(QEvent *event) override
    {
        Q_UNUSED(event);
        _isHovering = true;
        update();
    }

    void leaveEvent(QEvent *event) override
    {
        Q_UNUSED(event);
        _isHovering = false;
        update();
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event);
        _isHoldDown = true;
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        Q_UNUSED(event);
        _isHoldDown = false;
        Q_EMIT Clicked();
        update();
    }
};

//////////////////////////////////////////////////

enum class NewVersionAction {
    Update,
    Skip,
    Later,
};

NewVersionAction NewVersionMessageBox(
    QWidget *parent, const QString &title, const QString &text,
    const Core::Update::ReleaseInfo &releaseInfo)
{
    QMessageBox msgBox{QMessageBox::Question, title, text, QMessageBox::NoButton, parent};

    const auto buttonUpdate = msgBox.addButton(QMessageBox::tr("Update now"), QMessageBox::YesRole);
    const auto buttonSkip =
        msgBox.addButton(QMessageBox::tr("Skip this version"), QMessageBox::AcceptRole);
    const auto buttonView = msgBox.addButton(QMessageBox::tr("View release"), QMessageBox::NoRole);
    const auto buttonLater =
        msgBox.addButton(QMessageBox::tr("Remind me later"), QMessageBox::NoRole);

    msgBox.setDefaultButton(buttonUpdate);

    buttonView->disconnect();
    msgBox.connect(buttonView, &QPushButton::clicked, &msgBox, [&] { releaseInfo.OpenUrl(); });

    if (msgBox.exec() == -1) {
        return NewVersionAction::Later;
    }

    const auto clickedButton = msgBox.clickedButton();

    if (clickedButton == buttonUpdate) {
        return NewVersionAction::Update;
    }
    else if (clickedButton == buttonSkip) {
        return NewVersionAction::Skip;
    }
    else {
        return NewVersionAction::Later;
    }
}

//////////////////////////////////////////////////

MainWindow::MainWindow(QWidget *parent) : QDialog{parent}
{
    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");
    qRegisterMetaType<Core::Update::ReleaseInfo>("Core::Update::ReleaseInfo");
    qRegisterMetaType<Core::AirPods::NoiseControlMode>("Core::AirPods::NoiseControlMode");

    _closeButton = new CloseButton{this};
    _controlsButton = new ControlsButton{this};

    _ui.setupUi(this);

    setFixedSize(_windowSize);
    setWindowFlags(windowFlags() | Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    // Translucent window allows genuine Apple 28 px continuous squircle corners + soft drop
    // shadow + 1 px Liquid Glass specular rim without DWM's 8 px rectangular clamp.
    setAttribute(Qt::WA_TranslucentBackground);

    winId(); // realises the native window, so windowHandle() below is valid
    _ui.deviceLabel->setGeometry(58, 30, _windowSize.width() - 116, 40);
    _ui.deviceLabel->setFont(Theme::Font::Title());
    _ui.deviceLabel->SetRole(Widget::Label::Role::Primary);

    _ui.gridLayoutWidget->setGeometry((_windowSize.width() - 244) / 2, 94, 244, 106);
    _ui.podsBatteryContainer->setGeometry(56, 206, 144, 54);
    _ui.caseBatteryContainer->setGeometry(202, 206, 84, 54);
    _ui.gridLayoutWidget_2->hide();

    // Mathematically concentric circular corner buttons: 28 px card corner - 14 px inset = 14 px
    // button radius (28x28 diameter).
    const int cornerInset = _shadowMargin + 14;
    _controlsButton->move(cornerInset, cornerInset);
    _closeButton->move(_windowSize.width() - cornerInset - _closeButton->width(), cornerInset);
    connect(_controlsButton, &ControlsButton::Clicked, this, [this] {
        emit OpenControlSettingsRequested();
    });

    // Subtle secondary line below the title for nearby status, Hands-Free alerts, heart rate
    // BPM, or runtime estimates.
    _statusBadge = new Widget::Label{{}, Widget::Label::Role::Secondary, this};
    _statusBadge->setFont(Theme::Font::Footnote());
    _statusBadge->setAlignment(Qt::AlignCenter);
    _statusBadge->setGeometry(30, 72, _windowSize.width() - 60, 18);
    _statusBadge->hide();

    // Apple iOS 27 Liquid Glass Control Center bar: revealed when an AAP control transport is
    // connected and the model supports noise control.
    _noiseControl = new Widget::AppleNoiseControl{
        Widget::AppleNoiseControl::LayoutMode::FullControlCenter, this};
    _noiseControl->hide();
    connect(
        _noiseControl, &Widget::AppleNoiseControl::NoiseControlModeRequested, this,
        &MainWindow::NoiseControlModeRequested);
    connect(
        _noiseControl, &Widget::AppleNoiseControl::AdaptiveNoiseLevelRequested, this,
        &MainWindow::AdaptiveNoiseLevelRequested);
    connect(
        _noiseControl, &Widget::AppleNoiseControl::ConversationalAwarenessToggled, this,
        &MainWindow::ConversationalAwarenessRequested);
    connect(
        _noiseControl, &Widget::AppleNoiseControl::PersonalizedVolumeToggled, this,
        &MainWindow::PersonalizedVolumeRequested);
    connect(
        _noiseControl, &Widget::AppleNoiseControl::HearingAidToggled, this,
        &MainWindow::HearingAidRequested);
    connect(
        _noiseControl, &Widget::AppleNoiseControl::MoreControlsRequested, this,
        &MainWindow::OpenControlSettingsRequested);

    // Compact Apple Health Heart Rate pill bar for optical HR-capable models (AirPods Pro 3,
    // Powerbeats Pro 2).
    _heartRateBar = new Widget::PopupHeartRateBar{this};
    _heartRateBar->hide();
    connect(
        _heartRateBar, &Widget::PopupHeartRateBar::ToggleRequested, this,
        &MainWindow::HeartRateMonitoringRequested);
    connect(
        _heartRateBar, &Widget::PopupHeartRateBar::OpenHealthRequested, this,
        &MainWindow::OpenHealthSettingsRequested);

    for (auto *battery : {_leftBattery, _rightBattery, _caseBattery}) {
        battery->setFont(Theme::Font::Caption());
    }

    connect(qApp, &QGuiApplication::applicationStateChanged, this, &MainWindow::OnAppStateChanged);
    connect(_ui.pushButton, &QPushButton::clicked, this, &MainWindow::OnButtonClicked);
    connect(&_posAnimation, &QPropertyAnimation::finished, this, &MainWindow::OnPosMoveFinished);
    connect(_deviceView, &Widget::DeviceView::Clicked, this, &MainWindow::OnAnimationClicked);
    // The view refits when first shown on a screen, and on a change of display scale; the
    // rings follow.
    connect(_deviceView, &Widget::DeviceView::ContentRectChanged, this, [this] {
        PlaceBatteries(_viewModel.Present().artwork);
    });
    connect(_closeButton, &CloseButton::Clicked, this, [this] { DoHide(true); });

    connect(this, &MainWindow::UpdateStateSafely, this, &MainWindow::UpdateState);
    connect(this, &MainWindow::AvailableSafely, this, &MainWindow::Available);
    connect(this, &MainWindow::UnavailableSafely, this, &MainWindow::Unavailable);
    connect(this, &MainWindow::DisconnectSafely, this, &MainWindow::Disconnect);
    connect(this, &MainWindow::BindSafely, this, &MainWindow::Bind);
    connect(this, &MainWindow::UnbindSafely, this, &MainWindow::Unbind);
    connect(this, &MainWindow::ShowSafely, this, [this] { ShowPopup(false); });
    connect(this, &MainWindow::HideSafely, this, [this] { DoHide(false); });

    // Derived from the display rather than fixed, and zero when the user has turned
    // animations off. See Core::WindowEffects::AnimationDuration.
    _posAnimation.setDuration(
        Core::WindowEffects::AnimationDuration(windowHandle(), Theme::Duration::WindowMove));
    _autoHideTimer->callOnTimeout([this] { DoHide(false); });
    _transientStatusTimer->setSingleShot(true);
    _transientStatusTimer->callOnTimeout([this] {
        _viewModel.SetTransientStatus({});
        Repaint();
    });
    _ui.layoutAnimation->addWidget(_deviceView);
    _ui.layoutPods->setSpacing(Theme::Metric::PodRingGap);
    _ui.layoutPods->addWidget(_leftBattery);
    _ui.layoutPods->addWidget(_rightBattery);
    _ui.layoutCase->addWidget(_caseBattery);

    _ui.layoutAnimation->activate();
    _podsHome = _ui.podsBatteryContainer->pos();
    _caseHome = _ui.caseBatteryContainer->pos();
    _buttonHome = QRect{30, 238, _windowSize.width() - 60, 42};
    _ui.pushButton->SetPill(true);
    _ui.pushButton->setGeometry(_buttonHome);
}

MainWindow::~MainWindow()
{
    _deviceQueryThread.request_stop();
    if (_deviceQueryThread.joinable()) {
        _deviceQueryThread.join();
    }
}

void MainWindow::UpdateState(const Core::AirPods::State &state)
{
    LOG(Trace, "MainWindow::UpdateState");

    _viewModel.UpdateState(state);
    Repaint();
}

void MainWindow::UpdateNearbyState(const Core::AirPods::State &state, bool shouldAutoShow)
{
    LOG(Trace, "MainWindow::UpdateNearbyState (autoShow={})", shouldAutoShow);

    _viewModel.UpdateNearbyState(state);
    Repaint();

    if (!shouldAutoShow) {
        return;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!state.caseBox.isLidOpened && now < _nearbyDismissedUntil) {
        LOG(Trace, "Suppressing nearby auto-popup during dismiss cooldown.");
        return;
    }

    if (state.caseBox.isLidOpened) {
        _nearbyDismissedUntil = {};
    }
    ShowPopup(false);
}

void MainWindow::ClearNearby()
{
    LOG(Trace, "MainWindow::ClearNearby");

    const bool wasNearby = _viewModel.Present().isNearby;
    _viewModel.ClearNearby();
    Repaint();
    if (wasNearby && _isVisible && !_isHovered) {
        DoHide(false);
    }
}

void MainWindow::Available()
{
    LOG(Info, "MainWindow::Available");

    _viewModel.Available();
    Repaint();
}

void MainWindow::Unavailable()
{
    LOG(Info, "MainWindow::Unavailable");

    _viewModel.Unavailable();
    Repaint();
}

void MainWindow::Disconnect()
{
    LOG(Info, "MainWindow::Disconnect");

    _viewModel.Disconnect();
    Repaint();
}

void MainWindow::Bind()
{
    LOG(Info, "MainWindow::Bind");

    _viewModel.Bind();
    Repaint();
}

void MainWindow::Unbind()
{
    LOG(Info, "MainWindow::Unbind");

    _viewModel.Unbind();
    Repaint();
}

void MainWindow::SetBoundDevice(Core::AirPods::Model model, const QString &displayName)
{
    _viewModel.SetBoundDevice(model, displayName);
    Repaint();
}

void MainWindow::ShowPopup(bool activate)
{
    _nearbyDismissedUntil = {};

    if (!isVisible()) {
        show();
        raise();
        if (activate) {
            activateWindow();
        }
        return;
    }

    if (!_isVisible) {
        // Re-opening while a hide slide animation is still in flight: reverse smoothly back
        // into the screen corner instead of letting `OnPosMoveFinished` hide the window.
        _isVisible = true;
        _posAnimation.stop();
        AnchorToCurrentScreen();
    }

    ControlAutoHideTimer(true);
    raise();
    if (activate) {
        activateWindow();
    }
}

void MainWindow::ToggleVisibility()
{
    if (_isVisible && isVisible()) {
        DoHide(true);
    }
    else {
        ShowPopup(true);
    }
}

void MainWindow::SetLowBatteryThreshold(uint32_t threshold)
{
    for (auto *battery : {_leftBattery, _rightBattery, _caseBattery}) {
        battery->setAlarmValue(threshold);
    }
}

void MainWindow::SetQuickConnectAvailable(bool available)
{
    _viewModel.SetQuickConnectAvailable(available);
    Repaint();
}

void MainWindow::SetShowHeartRateInPopup(bool show)
{
    _viewModel.SetShowHeartRateInPopup(show);
    Repaint();
}

void MainWindow::SetConnecting(bool connecting)
{
    if (connecting) {
        _transientStatusTimer->stop();
    }
    _viewModel.SetConnecting(connecting);
    Repaint();
}

void MainWindow::SetTransientStatus(const QString &status)
{
    _transientStatusTimer->stop();
    _viewModel.SetTransientStatus(status);
    Repaint();
    if (!status.isEmpty()) {
        _transientStatusTimer->start(4500);
    }
}

void MainWindow::AskUserUpdate(const Core::Update::ReleaseInfo &releaseInfo)
{
    auto releaseVersion = releaseInfo.version.toString();

    QString changeLogBlock;
    if (!releaseInfo.changeLog.isEmpty()) {
        changeLogBlock = QString{"\n\n%1\n%2"}.arg(tr("Change log:")).arg(releaseInfo.changeLog);
    }

    auto action = NewVersionMessageBox(
        nullptr, Config::ProgramName,
        tr("Hey! I found a new version available!\n"
           "\n"
           "Current version: %1\n"
           "Latest version: %2"
           "%3")
            .arg(Core::Update::GetLocalVersion().toString())
            .arg(releaseVersion)
            .arg(changeLogBlock),
        releaseInfo);

    switch (action) {
    case Gui::NewVersionAction::Update:
        LOG(Info, "VersionUpdate: User clicked Update.");

        if (!releaseInfo.CanAutoUpdate()) {
            LOG(Info, "VersionUpdate: Cannot auto update; opening release page.");
            releaseInfo.OpenUrl();
            return;
        }

        // Only exit AirpodsBridge once the downloaded installer has actually launched.
        if (Gui::DownloadWindow{releaseInfo}.exec() == QDialog::Accepted) {
            Utils::Qt::QuitApplicationSafely();
        }
        return;

    case Gui::NewVersionAction::Skip:
        LOG(Info, "VersionUpdate: User clicked Skip.");

        Core::Settings::ModifiableAccess()->skipped_version = releaseVersion;
        break;

    case Gui::NewVersionAction::Later:
        LOG(Info, "VersionUpdate: User clicked Later.");

        emit UpdateDeferred();
        break;

    default:
        LOG(Warn, "VersionUpdate: Unhandled user clicked button.");
        break;
    }
}

void MainWindow::ChangeButtonAction(ButtonAction action, bool nearbyLayout, bool connecting)
{
    _buttonAction = action;
    switch (action) {
    case ButtonAction::NoButton:
        _ui.pushButton->setText("");
        _ui.pushButton->SetPrimary(false);
        _ui.pushButton->setEnabled(true);
        _ui.pushButton->hide();
        return;

    case ButtonAction::Bind:
        _ui.pushButton->setText(tr("Bind to AirPods"));
        _ui.pushButton->SetPrimary(true);
        _ui.pushButton->SetPill(true);
        _ui.pushButton->setEnabled(true);
        break;

    case ButtonAction::Connect:
        _ui.pushButton->setText(connecting ? tr("Connecting...") : tr("Connect"));
        _ui.pushButton->SetPrimary(true);
        _ui.pushButton->SetPill(true);
        _ui.pushButton->setEnabled(!connecting);
        break;

    default:
        FatalError(std::format("Unhandled ButtonAction: '{}'", Helper::ToUnderlying(action)), true);
    }

    if (nearbyLayout) {
        _ui.pushButton->setGeometry(28, 274, width() - 56, 42);
    }
    else {
        _ui.pushButton->setGeometry(_buttonHome);
    }
    _ui.pushButton->show();
}

void MainWindow::BindDevice()
{
    LOG(Info, "BindDevice");

    if (_deviceQueryRunning.exchange(true)) {
        LOG(Info, "Ignore duplicate device query while one is already running.");
        return;
    }

    if (_deviceQueryThread.joinable()) {
        _deviceQueryThread.join();
    }

    _deviceQueryThread = std::jthread{[this](std::stop_token stopToken) {
        Support::OS::Windows::Winrt::Initialize();
        auto devices = Core::AirPods::GetDevices();
        if (stopToken.stop_requested()) {
            return;
        }

        QMetaObject::invokeMethod(
            this,
            [this, devices = std::move(devices)]() mutable {
                _deviceQueryRunning = false;
                ShowDeviceSelector(std::move(devices));
            },
            Qt::QueuedConnection);
    }};
}

void MainWindow::ShowDeviceSelector(std::vector<Core::Bluetooth::Device> devices)
{
    if (devices.empty()) {
        QMessageBox::warning(
            this, Config::ProgramName,
            QMessageBox::tr("No paired device found.\n"
                            "You need to pair your AirPods in Windows Bluetooth Settings first."));
        return;
    }

    // Prioritize recognized Apple audio hardware (VID 0x004C / known AirPods & Beats PIDs)
    // at the top of the bind picker so paired phones or controllers stay out of the way.
    std::stable_sort(
        devices.begin(), devices.end(),
        [](const Core::Bluetooth::Device &a, const Core::Bluetooth::Device &b) {
            const auto score = [](const Core::Bluetooth::Device &d) {
                const bool knownModel = Core::AirPods::ModelFromProductId(d.GetProductId()) !=
                                        Core::AirPods::Model::Unknown;
                const bool isApple = d.GetVendorId() == Core::AirPods::kAppleVendorId;
                return (knownModel ? 2 : 0) + (isApple ? 1 : 0);
            };
            return score(a) > score(b);
        });

    int selectedIndex = 0;

    if (devices.size() > 1) {
        QStringList deviceNames;
        for (const auto &device : devices) {
            auto deviceName = device.GetName();
            const uint16_t pid = device.GetProductId();
            const uint16_t vid = device.GetVendorId();
            const auto detectedModel = Core::AirPods::ModelFromProductId(pid);

            LOG(Trace, "Device name: '{}'", deviceName);
            LOG(Trace, "GetProductId: '{}' GetVendorId: '{}'", pid, vid);

            QString label = QString::fromStdString(deviceName);
            if (detectedModel != Core::AirPods::Model::Unknown) {
                const QString modelStr = Helper::ToString(detectedModel);
                if (!label.contains(modelStr, Qt::CaseInsensitive)) {
                    label = QStringLiteral("%1  (%2)").arg(label, modelStr);
                }
            }
            deviceNames.append(label);
        }

        SelectWindow selector{tr("Please select your AirPods device below."), deviceNames, this};
        if (selector.exec() == -1) {
            LOG(Warn, "selector.exec() == -1");
            return;
        }

        if (!selector.HasResult()) {
            LOG(Info, "No result for selector.");
            return;
        }

        selectedIndex = selector.GetSelectedIndex();
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(devices.size())) {
            LOG(Warn, "Selector returned out-of-range index '{}'.", selectedIndex);
            return;
        }
    }

    const auto &selectedDevice = devices.at(static_cast<size_t>(selectedIndex));
    const auto name = QString::fromStdString(selectedDevice.GetName());
    const auto containerId = QString::fromStdString(selectedDevice.GetContainerId());

    LOG(Info, "Selected device index: '{}', device name: '{}'. Bound to this device.",
        selectedIndex, selectedDevice.GetName());

    auto settings = Core::Settings::ModifiableAccess();
    settings->device_address = selectedDevice.GetAddress();
    settings->bound_device_name = name;
    // Automatically link Quick Connect when the user has not chosen a device yet so they do
    // not have to pick the same AirPods twice.
    if (settings->tray_quick_connect_device_id.isEmpty() && !containerId.isEmpty()) {
        settings->tray_quick_connect_device_id = containerId;
    }
}

void MainWindow::ControlAutoHideTimer(bool start)
{
    LOG(Trace, "ControlAutoHideTimer: start == '{}', _isVisible == '{}'", start, _isVisible);

    if (start && _isVisible && !_isHovered) {
        _autoHideTimer->start(10s);
    }
    else {
        _autoHideTimer->stop();
    }
}

void MainWindow::Repaint()
{
    const auto presentation = _viewModel.Present();
    _ui.deviceLabel->setText(presentation.title);
    FitDeviceLabelFont();

    _statusBadge->setText(presentation.statusBadge);
    _statusBadge->setVisible(!presentation.statusBadge.isEmpty());

    UpdateLayoutAndControls(presentation);

    _deviceView->SetArtwork(presentation.artwork);
    PlaceBatteries(presentation.artwork);

    const auto applyBattery = [](Widget::Battery *widget, const BatteryPresentation &battery) {
        if (!battery.visible) {
            widget->hide();
            return;
        }

        widget->setCharging(battery.charging);
        widget->setValue(battery.value);
        widget->show();
    };

    applyBattery(_leftBattery, presentation.leftBattery);
    applyBattery(_rightBattery, presentation.rightBattery);
    applyBattery(_caseBattery, presentation.caseBattery);
}

void MainWindow::UpdateLayoutAndControls(const MainWindowPresentation &presentation)
{
    const bool hasAnyBattery = presentation.leftBattery.visible ||
                               presentation.rightBattery.visible ||
                               presentation.caseBattery.visible;
    const bool nearbyWithButton = presentation.isNearby && hasAnyBattery &&
                                  presentation.buttonAction != ButtonAction::NoButton;

    QSize targetSize = _windowSize;
    if (presentation.noiseControl.visible && presentation.heartRate.visible) {
        targetSize = _expandedWithHeartRateWindowSize;
    }
    else if (presentation.noiseControl.visible) {
        targetSize = _expandedWindowSize;
    }
    else if (presentation.heartRate.visible) {
        targetSize = _compactWithHeartRateWindowSize;
    }
    else if (nearbyWithButton) {
        targetSize = _nearbyWindowSize;
    }

    const bool resized = (size() != targetSize);
    if (resized) {
        setFixedSize(targetSize);
        if (_isVisible && isVisible() && _posAnimation.state() != QAbstractAnimation::Running) {
            AnchorToCurrentScreen();
        }
    }

    ChangeButtonAction(presentation.buttonAction, nearbyWithButton, presentation.connecting);

    int nextY = 268;
    if (presentation.noiseControl.visible) {
        _noiseControl->ApplyPresentation(presentation.noiseControl);
        const int ncHeight = _noiseControl->sizeHint().height();
        _noiseControl->setGeometry(28, nextY, targetSize.width() - 56, ncHeight);
        _noiseControl->show();
        nextY += ncHeight + 10;
    }
    else {
        _noiseControl->hide();
    }

    if (presentation.heartRate.visible) {
        _heartRateBar->ApplyPresentation(presentation.heartRate);
        _heartRateBar->setGeometry(
            28, nextY, targetSize.width() - 56, _heartRateBar->sizeHint().height());
        _heartRateBar->show();
    }
    else {
        _heartRateBar->hide();
    }
}

void MainWindow::AnchorToCurrentScreen()
{
    auto *targetScreen = screen();
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen == nullptr) {
        return;
    }

    const auto availableGeometry = targetScreen->availableGeometry();
    const auto screenGeometry = targetScreen->geometry();

    int targetX = availableGeometry.right() - width() + 1 - _screenMargin.width();
    int targetY = availableGeometry.bottom() - height() + 1 - _screenMargin.height();

    if (availableGeometry.top() > screenGeometry.top()) {
        targetY = availableGeometry.top() + _screenMargin.height();
    }
    else if (availableGeometry.left() > screenGeometry.left()) {
        targetX = availableGeometry.left() + _screenMargin.width();
    }

    move(targetX, targetY);
}

void MainWindow::PlaceBatteries(const std::optional<DeviceArtwork::Artwork> &artwork)
{
    // The rings sit under what they measure: the pair under the buds, the case's under the
    // case, a one-piece device's one ring under it. The artwork says where those are, as
    // fractions of its width, and the view says where the artwork landed. Apple's
    // pictures place the buds and the case differently from model to model, which is why
    // this is not a fixed position.
    const auto content = _deviceView->ContentRect();
    if (!artwork.has_value() || artwork->anchors.empty() || content.isEmpty()) {
        _ui.podsBatteryContainer->move(_podsHome);
        _ui.caseBatteryContainer->move(_caseHome);
        return;
    }

    const auto anchorX = [&](qreal fraction) {
        const auto x = qRound(content.left() + fraction * content.width());
        return _deviceView->mapTo(this, QPoint{x, 0}).x();
    };
    const auto centreOn = [](QWidget *container, int x) {
        container->move(x - container->width() / 2, container->y());
    };
    centreOn(_ui.podsBatteryContainer, anchorX(artwork->anchors.front()));
    centreOn(_ui.caseBatteryContainer, anchorX(artwork->anchors.back()));
}

void MainWindow::FitDeviceLabelFont()
{
    auto font = _ui.deviceLabel->font();
    font.setPointSize(_deviceLabelMaximumPointSize);

    const auto availableWidth = _ui.deviceLabel->contentsRect().width();
    while (font.pointSize() > _deviceLabelMinimumPointSize &&
           QFontMetrics{font}.horizontalAdvance(_ui.deviceLabel->text()) > availableWidth)
    {
        font.setPointSize(font.pointSize() - 1);
    }

    _ui.deviceLabel->setFont(font);
}

void MainWindow::OnAppStateChanged(Qt::ApplicationState state)
{
    LOG(Trace, "OnAppStateChanged: '{}'", Helper::ToString(state));
    ControlAutoHideTimer(state != Qt::ApplicationActive);
}

void MainWindow::OnPosMoveFinished()
{
    if (!_isVisible) {
        hide();
    }
}

void MainWindow::OnAnimationClicked()
{
#if defined APB_DEBUG
    using namespace Core::AirPods;

    static Model next = Model::AirPods_1;

    _ui.deviceLabel->setText(Helper::ToString(next));
    const auto artwork = DeviceArtwork::Find(next);
    _deviceView->SetArtwork(artwork);
    PlaceBatteries(artwork);

    next = static_cast<Model>(Helper::ToUnderlying(next) + 1);
    if (next >= Model::_Max) {
        next = Model::AirPods_1;
    }
#endif
}

void MainWindow::OnButtonClicked()
{
    switch (_buttonAction) {
    case ButtonAction::Bind:
        LOG(Info, "User clicked 'Bind'");
        BindDevice();
        break;

    case ButtonAction::Connect:
        LOG(Info, "User clicked 'Connect'");
        emit QuickConnectRequested();
        break;

    default:
        FatalError(
            std::format("Unhandled ButtonAction: '{}'", Helper::ToUnderlying(_buttonAction)), true);
    }
}

void MainWindow::DoHide(bool userDismissed)
{
    LOG(Trace, "MainWindow: Hide (userDismissed={})", userDismissed);

    if (userDismissed) {
        _nearbyDismissedUntil = std::chrono::steady_clock::now() + 45s;
    }

    if (!_isVisible) {
        return;
    }
    _isVisible = false;

    ControlAutoHideTimer(false);

    auto *currentScreen = screen();
    if (currentScreen == nullptr) {
        currentScreen = QGuiApplication::primaryScreen();
    }
    if (currentScreen == nullptr) {
        hide();
        return;
    }

    const auto availableGeometry = currentScreen->availableGeometry();
    const auto screenGeometry = currentScreen->geometry();

    QPoint hidePos{x(), screenGeometry.bottom() + 1};
    if (availableGeometry.top() > screenGeometry.top()) {
        hidePos = QPoint{x(), screenGeometry.top() - height()};
    }
    else if (availableGeometry.left() > screenGeometry.left()) {
        hidePos = QPoint{screenGeometry.left() - width(), y()};
    }

    _posAnimation.stop();
    _posAnimation.setEasingCurve(QEasingCurve::InExpo);
    _posAnimation.setStartValue(pos());
    _posAnimation.setEndValue(hidePos);
    _posAnimation.start();
}

void MainWindow::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHint(QPainter::Antialiasing, true);

    const auto &colours = Theme::Colours();
    const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;

    const qreal pad = static_cast<qreal>(_shadowMargin);
    const QRectF cardRect{
        pad + 0.5, pad + 0.5, static_cast<qreal>(width()) - pad * 2.0 - 1.0,
        static_cast<qreal>(height()) - pad * 2.0 - 1.0};

    // Soft Apple ambient drop shadow (multi-layered continuous squircles).
    painter.setPen(Qt::NoPen);
    for (int i = 4; i >= 1; --i) {
        const qreal spread = static_cast<qreal>(i) * 2.1;
        const int alpha = isDark ? (34 - i * 6) : (20 - i * 4);
        painter.setBrush(QColor{0, 0, 0, std::max(4, alpha)});
        painter.drawPath(Painting::ContinuousRoundedRect(
            cardRect.adjusted(-spread, -spread + 2.0, spread, spread + 2.5),
            _cardCornerRadius + spread));
    }

    // Apple 28 px continuous squircle surface with subtle Liquid Glass sheen.
    const QPainterPath cardPath = Painting::ContinuousRoundedRect(cardRect, _cardCornerRadius);
    QLinearGradient surfaceGrad{cardRect.topLeft(), cardRect.bottomLeft()};
    if (isDark) {
        surfaceGrad.setColorAt(0.0, QColor{38, 38, 42, 246});
        surfaceGrad.setColorAt(1.0, QColor{26, 26, 30, 250});
    }
    else {
        surfaceGrad.setColorAt(0.0, QColor{255, 255, 255, 252});
        surfaceGrad.setColorAt(1.0, QColor{247, 247, 250, 252});
    }
    painter.setBrush(surfaceGrad);
    painter.drawPath(cardPath);

    // Crisp 1 px Liquid Glass specular border around the 28 px squircle.
    QLinearGradient borderGrad{cardRect.topLeft(), cardRect.bottomRight()};
    if (isDark) {
        borderGrad.setColorAt(0.0, QColor{255, 255, 255, 76});
        borderGrad.setColorAt(0.45, QColor{255, 255, 255, 18});
        borderGrad.setColorAt(1.0, QColor{255, 255, 255, 42});
    }
    else {
        borderGrad.setColorAt(0.0, QColor{255, 255, 255, 235});
        borderGrad.setColorAt(0.5, QColor{0, 0, 0, 22});
        borderGrad.setColorAt(1.0, QColor{0, 0, 0, 32});
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{QBrush{borderGrad}, 1.0});
    painter.drawPath(cardPath);
    Q_UNUSED(colours);
}

void MainWindow::enterEvent(QEvent *event)
{
    QDialog::enterEvent(event);
    _isHovered = true;
    ControlAutoHideTimer(false);
}

void MainWindow::leaveEvent(QEvent *event)
{
    QDialog::leaveEvent(event);
    _isHovered = false;
    ControlAutoHideTimer(true);
}

void MainWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    LOG(Trace, "MainWindow: Show");

    if (_isVisible) {
        return;
    }
    _isVisible = true;

    ControlAutoHideTimer(true);

    auto targetScreen = QGuiApplication::screenAt(QCursor::pos());
    if (targetScreen == nullptr) {
        targetScreen = screen();
    }
    if (targetScreen == nullptr) {
        targetScreen = QGuiApplication::primaryScreen();
    }
    if (targetScreen == nullptr) {
        return;
    }

    const auto availableGeometry = targetScreen->availableGeometry();
    const auto screenGeometry = targetScreen->geometry();

    int targetX = availableGeometry.right() - width() + 1 - _screenMargin.width();
    int targetY = availableGeometry.bottom() - height() + 1 - _screenMargin.height();
    QPoint startPos{targetX, screenGeometry.bottom() + 1};

    if (availableGeometry.top() > screenGeometry.top()) {
        targetY = availableGeometry.top() + _screenMargin.height();
        startPos = QPoint{targetX, screenGeometry.top() - height()};
    }
    else if (availableGeometry.left() > screenGeometry.left()) {
        targetX = availableGeometry.left() + _screenMargin.width();
        startPos = QPoint{screenGeometry.left() - width(), targetY};
    }

    move(startPos);

    // The window may have moved to a display with a different refresh rate.
    _posAnimation.setDuration(
        Core::WindowEffects::AnimationDuration(windowHandle(), Theme::Duration::WindowMove));

    _posAnimation.stop();
    _posAnimation.setEasingCurve(QEasingCurve::OutExpo);
    _posAnimation.setStartValue(startPos);
    _posAnimation.setEndValue(QPoint{targetX, targetY});
    _posAnimation.start();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // The application owns `MainWindow` via `std::unique_ptr`: closing via Alt+F4 or the
    // shell must hide the popup rather than deleting it out from under its owner.
    event->ignore();
    DoHide(true);
}
} // namespace Gui

#include "MainWindow.moc"
