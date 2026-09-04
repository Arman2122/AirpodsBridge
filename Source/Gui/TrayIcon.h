// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QSystemTrayIcon>
#include <QTimer>
#include <QMenu>
#include <QAction>

#include "Source/Core/AirPods.h"
#include "Source/Core/QuickConnect.h"
#include "Source/Core/Update.h"
#include "Source/Presentation/Base.h"
#include "Source/Gui/TrayActivation.h"
#include "Source/Gui/SettingsWindow.h"

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
    void VersionUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo);
    void ShowContextMenu(const QPoint &position);

Q_SIGNALS:
    void OnTrayIconBatteryChangedSafely(Core::Settings::TrayIconBatteryBehavior value);
    void ShowMainWindowRequested();
    void UserUpdateRequested(const Core::Update::ReleaseInfo &releaseInfo);
    void ToolTipChanged(const QString &text);

private:
    struct IconRenderState {
        std::optional<QString> text;
        bool showUpdateDot{false};

        bool operator==(const IconRenderState &rhs) const = default;
    };

    QSystemTrayIcon *_tray = new QSystemTrayIcon{this};
    QMenu *_menu = new QMenu{this};
    QAction *_actionNewVersion = new QAction{tr("New version available!"), this};
    QAction *_actionSettings = new QAction{tr("Settings"), this};
    QAction *_actionAbout = new QAction{tr("About"), this};
    QAction *_actionQuit = new QAction{tr("Quit"), this};
    Core::Settings::TrayIconBatteryBehavior _trayIconBatteryBehavior{
        Core::Settings::TrayIconBatteryBehavior::Disable};
    Status _status{Status::Unavailable};
    std::optional<Core::AirPods::State> _airPodsState;
    std::optional<QString> _displayName;
    std::optional<Core::Update::ReleaseInfo> _updateReleaseInfo;
    std::optional<IconRenderState> _lastIconRenderState;
    Core::QuickConnect::Controller &_quickConnect;
    TrayActivationState _trayActivationState;
    QTimer _singleClickTimer{this};

    void ShowMainWindow();
    void Repaint();
    void ExecuteTrayActivation(TrayActivationAction action);

    static std::optional<QImage>
    GenerateIcon(int size, const std::optional<QString> &optText, const std::optional<QColor> &dot);

    void OnNewVersionClicked();
    void OnSettingsClicked();
    void OnAboutClicked();
    void OnIconClicked(QSystemTrayIcon::ActivationReason reason);
    void OnQuickConnectOutcome(Core::QuickConnect::Outcome outcome, const QString &deviceName);
    void OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior value);

protected:
    SettingsWindow _settingsWindow;
};
} // namespace Gui
