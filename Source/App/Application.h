// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <memory>
#include <SingleApplication>

#include "Source/Core/Settings.h"
#include "Source/Support/Opts.h"

namespace Core::AirPods {
class Manager;
}

namespace Core::AutoStart {
class Service;
}

namespace Core::QuickConnect {
class Backend;
class Controller;
} // namespace Core::QuickConnect

namespace Gui {
class DownloadWindow;
class MainWindow;
class TaskbarStatus;
class TrayIcon;
} // namespace Gui

class ApbApplication : public SingleApplication, private Core::Settings::ApplyObserver
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
    std::unique_ptr<Gui::TrayIcon> _trayIcon;
    std::unique_ptr<Gui::TaskbarStatus> _taskbarStatus;
    std::unique_ptr<Gui::MainWindow> _mainWindow;
    std::unique_ptr<Gui::DownloadWindow> _downloadWindow;
    std::unique_ptr<Core::AirPods::Manager> _airPodsManager;
    std::unique_ptr<Core::AutoStart::Service> _autoStartService;

    void InitSettings(Core::Settings::LoadResult loadResult);
    void FirstTimeUse();

    void ConnectAirPodsManager();
    void ConnectGuiComponents();

    void OnAutoRunChanged(bool enable) override;
    void OnAutomaticEarDetectionChanged(bool enable) override;
    void OnRssiMinChanged(int16_t rssiMin) override;
    void OnDeviceAddressChanged(uint64_t address) override;
    void OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior behavior) override;
    void OnTrayQuickConnectEnabledChanged(bool enable) override;
    void OnTrayQuickConnectDeviceChanged(const QString &deviceId) override;
    void OnTaskbarBatteryChanged(Core::Settings::TaskbarStatusBehavior behavior) override;
};

#define ApbApp (dynamic_cast<ApbApplication *>(QCoreApplication::instance()))
