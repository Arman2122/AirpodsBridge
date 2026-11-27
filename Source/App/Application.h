// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <memory>
#include <vector>

#include <SingleApplication>

#include "Source/Core/AutomationEngine.h"
#include "Source/Core/Settings.h"
#include "Source/Support/Opts.h"

namespace Core::AirPods {
class Manager;
}

namespace Core::AudioControl {
class Service;
}

namespace Core::AutoStart {
class Service;
}

namespace Core::GlobalHotkeys {
class Manager;
}

namespace Core::Ipc {
class Server;
}

namespace Core::QuickConnect {
class Backend;
class Controller;
} // namespace Core::QuickConnect

namespace Core::SystemEvents {
class Watcher;
}

namespace Core::Update {
class AsyncChecker;
struct CheckResult;
struct ReleaseInfo;
} // namespace Core::Update

namespace Gui {
class DownloadWindow;
class FrameClock;
class MainWindow;
class TaskbarStatus;
class ThemeController;
class TrayIcon;
} // namespace Gui

//
// Owns every service and window, and is the one place that wires them together.
//
// Settings reach the services through `WireSettings`: one `Watch` per field, written next
// to the call it drives. Nothing else in the application subscribes to settings.
//
// It is also the automation engine's `Effects`: the engine decides, this class knows which
// service carries the decision out.
//
class ApbApplication : public SingleApplication, private Core::Automation::Effects
{
    Q_OBJECT

public:
    static void PreConstruction();
    ApbApplication(int argc, char *argv[]);
    ~ApbApplication() override;

    bool Prepare(int argc, char *argv[]);
    int Run();

    static void QuitSafely();

private:
    static inline Opts::LaunchOptsManager _launchOptsMgr;
    // Declared before the GUI members: `Gui::TrayIcon` and the `Gui::SettingsWindow` it owns hold a
    // reference to the controller, so the controller has to outlive them, and members are destroyed
    // in reverse declaration order.
    std::shared_ptr<Core::QuickConnect::Backend> _quickConnectBackend;
    std::unique_ptr<Core::QuickConnect::Controller> _quickConnect;
    // Before every window: it sets the style and the palette they are built with, and
    // must outlive them to keep their title bars right.
    std::unique_ptr<Gui::ThemeController> _theme;
    // Before every window as well: it drives their animations at the display's rate, and
    // must outlive them for the same reason.
    std::unique_ptr<Gui::FrameClock> _frameClock;
    std::unique_ptr<Gui::TrayIcon> _trayIcon;
    std::unique_ptr<Gui::TaskbarStatus> _taskbarStatus;
    std::unique_ptr<Gui::MainWindow> _mainWindow;
    std::unique_ptr<Gui::DownloadWindow> _downloadWindow;
    std::unique_ptr<Core::AirPods::Manager> _airPodsManager;
    std::unique_ptr<Core::AudioControl::Service> _audioControl;
    std::unique_ptr<Core::Automation::Engine> _automation;
    std::unique_ptr<Core::AutoStart::Service> _autoStartService;
    std::unique_ptr<Core::Update::AsyncChecker> _updateChecker;
    std::unique_ptr<Core::GlobalHotkeys::Manager> _hotkeys;
    std::unique_ptr<Core::Ipc::Server> _ipcServer;
    std::unique_ptr<Core::SystemEvents::Watcher> _systemEvents;
    // Declared last so that it is destroyed first: a watcher must never outlive the members
    // it captures.
    std::vector<Core::Settings::Subscription> _settingsWatches;
    bool _showPopupOnLidOpen{true};
    bool _autoEnableRadioService{true};
    bool _firstTimeSetupRan{false};
    bool _autoEnableRadioAttempted{false};
    bool _nativeToastNotifications{true};
    bool _updateCheckEnabled{false};
    bool _trayWelcomePending{false};
    bool _lastVirtualAdapterAttached{false};
    uint64_t _codecReconnectEpoch{0};
    uint64_t _radioReconcileEpoch{0};

    void InitSettings(Core::Settings::LoadResult loadResult);
    void FirstTimeUse();
    void EnsureRadioServiceEnabledOnStart();
    void ShowTrayWelcome();

    void ConnectAirPodsManager();
    void ConnectGuiComponents();
    void WireSettings();

    QByteArray HandleIpcRequest(const QByteArray &requestJson);
    QString BuildStatusJson() const;

    void SetUpdateCheckEnabled(bool enable);
    void OnUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo, bool silent);
    void OnManualUpdateCheckCompleted(const Core::Update::CheckResult &result);

    // Core::Automation::Effects
    void PlayMedia() override;
    void PauseMedia() override;
    void Notify(const QString &title, const QString &text) override;
    void ConnectDevice() override;
    void SwitchAudioOutputToAirPods() override;
    void RestorePreviousAudioOutput() override;
};

#define ApbApp (dynamic_cast<ApbApplication *>(QCoreApplication::instance()))
