// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/App/Application.h"

#include <QGuiApplication>
#include <QMessageBox>

#include <Config.h>
#include "Source/Support/Logger.h"
#include "Source/Support/Error.h"
#include "Source/Support/Utils.h"
#include "Source/Gui/Utils.h"
#include "Source/Gui/DownloadWindow.h"
#include "Source/Gui/MainWindow.h"
#include "Source/Gui/TaskbarStatus.h"
#include "Source/Gui/TrayIcon.h"
#include "Source/Core/AirPods.h"
#include "Source/Core/AutoStart.h"
#include "Source/Core/Bluetooth.h"
#include "Source/Core/GlobalMedia.h"
#include "Source/Core/QuickConnect.h"
#if defined APB_OS_WIN
    #include "Source/Core/QuickConnect_win.h"
#endif
#include "Source/Core/Settings.h"
#include "Source/Presentation/Theme.h"
#include "Source/Core/Update.h"

ApbApplication::~ApbApplication()
{
    Core::Settings::SetApplyObserver(nullptr);
}

void ApbApplication::PreConstruction()
{
    setAttribute(Qt::AA_DisableWindowContextHelpButton);
    setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
        Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

void ApbApplication::InitSettings(Core::Settings::LoadResult loadResult)
{
    const auto result = loadResult;

    switch (result) {
    case Core::Settings::LoadResult::AbiIncompatible:
        QMessageBox::information(
            nullptr, Config::ProgramName,
            tr("Settings format has changed a bit and needs to be reconfigured."));
        [[fallthrough]];

    case Core::Settings::LoadResult::NoAbiField:
        FirstTimeUse();
        break;

    case Core::Settings::LoadResult::Successful:
        break;

    default:
        FatalError(std::format("Unhandled LoadResult: '{}'", Helper::ToUnderlying(result)), true);
    }

    Core::Settings::Apply();
}

void ApbApplication::FirstTimeUse()
{
    const auto &constMetaFields = Core::Settings::GetConstMetaFields();

    QMessageBox::information(
        nullptr, Config::ProgramName,
        tr("Hello, welcome to %1!\n"
           "\n"
           "This seems to be your first time using this program.\n"
           "Let's configure something together.")
            .arg(Config::ProgramName));

    auto current = Core::Settings::GetCurrent();

    // Auto run
    //
    current.auto_run =
        QMessageBox::question(
            nullptr, Config::ProgramName,
            tr("Do you want this program to launch when the system starts?\n"
               "\n"
               "If you frequently use AirPods with this computer, it is recommended that you click "
               "\"Yes\"."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;

    Core::Settings::Save(std::move(current));

    QMessageBox::information(
        nullptr, Config::ProgramName,
        tr("Great! Everything is ready!\n"
           "\n"
           "Enjoy it all."));

    _trayIcon->ShowMessage(
        tr("You can find me in the system tray"),
        tr("Click the icon to view battery information, right-click to "
           "customize settings or quit."));
}

ApbApplication::ApbApplication(int argc, char *argv[]) : SingleApplication{argc, argv} {}

bool ApbApplication::Prepare(int argc, char *argv[])
{
    Error::Initialize();

    const auto &opts = _launchOptsMgr.Parse(argc, argv);

    Logger::Initialize(opts.enableTrace);

    LOG(Info, "Launched. Version: '{}'", Config::Version::String);
#if defined APB_BUILD_GIT_HASH
    LOG(Info, "Build git hash: '{}'", APB_BUILD_GIT_HASH);
#endif
#if defined APB_DEBUG
    LOG(Info, "Build configuration: Debug");
#else
    LOG(Info, "Build configuration: Not Debug");
#endif

    LOG(Info, "Opts: {}", opts);

    Logger::CleanUpOldLogFiles();

    // Registers the bundled faces and resolves the family, so every widget constructed
    // after this point inherits the interface font. See Source/Presentation/Theme.h.
    Gui::Theme::Font::Register();
    setFont(Gui::Theme::Font::Body());
    setWindowIcon(QIcon{Config::QrcIconSvg});
    setQuitOnLastWindowClosed(false);

#if defined APB_OS_WIN
    Support::OS::Windows::Winrt::Initialize();
#endif

    // Loaded before the GUI is constructed: `InitSettings` replays the result once the
    // windows exist, and the widgets read the already-loaded values while they are built.
    const auto settingsLoadResult = Core::Settings::Load();

#if defined APB_OS_WIN
    _quickConnectBackend = std::make_shared<Core::QuickConnect::WindowsBackend>();
#else
    _quickConnectBackend = std::make_shared<Core::QuickConnect::NullBackend>();
#endif
    _quickConnect = std::make_unique<Core::QuickConnect::Controller>(_quickConnectBackend);

    _trayIcon = std::make_unique<Gui::TrayIcon>(*_quickConnect);
    _taskbarStatus = std::make_unique<Gui::TaskbarStatus>();
    _mainWindow = std::make_unique<Gui::MainWindow>();
    _autoStartService = Core::AutoStart::CreateAutoStartService();
    _airPodsManager = std::make_unique<Core::AirPods::Manager>(this);

    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");

    ConnectGuiComponents();
    ConnectAirPodsManager();

    Core::Settings::SetApplyObserver(this);

    _mainWindow->Unavailable();
    _trayIcon->Unavailable();
    _taskbarStatus->Unavailable();

    InitSettings(settingsLoadResult);

    return true;
}

void ApbApplication::ConnectAirPodsManager()
{
    auto *manager = _airPodsManager.get();
    auto *mainWindow = _mainWindow.get();

    connect(
        manager, &Core::AirPods::Manager::BoundDeviceUnavailable, this,
        [] {
            auto settings = Core::Settings::ModifiableAccess();
            settings->device_address = 0;
        },
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, mainWindow, &Gui::MainWindow::UpdateState);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, _trayIcon.get(),
        &Gui::TrayIcon::UpdateState);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, _taskbarStatus.get(),
        &Gui::TaskbarStatus::UpdateState);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, mainWindow, &Gui::MainWindow::Disconnect);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, _trayIcon.get(),
        &Gui::TrayIcon::Disconnect);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, _taskbarStatus.get(),
        &Gui::TaskbarStatus::Disconnect);
    connect(
        manager, &Core::AirPods::Manager::ScannerAvailabilityChanged, mainWindow,
        [this, mainWindow](bool available) {
            if (available) {
                mainWindow->Available();
                _trayIcon->Disconnect();
                _taskbarStatus->Disconnect();
            }
            else {
                mainWindow->Unavailable();
                _trayIcon->Unavailable();
                _taskbarStatus->Unavailable();
            }
        });
    connect(manager, &Core::AirPods::Manager::LidToggled, mainWindow, [mainWindow](bool opened) {
        if (opened) {
            emit mainWindow->ShowSafely();
        }
        else {
            emit mainWindow->HideSafely();
        }
    });
}

void ApbApplication::ConnectGuiComponents()
{
    connect(
        _trayIcon.get(), &Gui::TrayIcon::ShowMainWindowRequested, _mainWindow.get(),
        &Gui::MainWindow::show);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::UserUpdateRequested, _mainWindow.get(),
        &Gui::MainWindow::AskUserUpdate);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::ToolTipChanged, _taskbarStatus.get(),
        &Gui::TaskbarStatus::SetToolTip);

    connect(
        _taskbarStatus.get(), &Gui::TaskbarStatus::ShowMainWindowRequested, _mainWindow.get(),
        &Gui::MainWindow::show);
    connect(
        _taskbarStatus.get(), &Gui::TaskbarStatus::ShowTrayMenuRequested, _trayIcon.get(),
        &Gui::TrayIcon::ShowContextMenu);

    connect(
        _mainWindow.get(), &Gui::MainWindow::SilentUpdateAvailable, _trayIcon.get(),
        &Gui::TrayIcon::VersionUpdateAvailable);
}

int ApbApplication::Run()
{
    _airPodsManager->StartScanner();
    return exec();
}

void ApbApplication::QuitSafely()
{
    Utils::Qt::QuitApplicationSafely();
}

void ApbApplication::OnAutoRunChanged(bool enable)
{
    _autoStartService->SetEnabled(enable);
}

void ApbApplication::OnAutomaticEarDetectionChanged(bool enable)
{
    _airPodsManager->OnAutomaticEarDetectionChanged(enable);
}

void ApbApplication::OnRssiMinChanged(int16_t rssiMin)
{
    _airPodsManager->OnRssiMinChanged(rssiMin);
}

void ApbApplication::OnDeviceAddressChanged(uint64_t address)
{
    if (address == 0) {
        emit _mainWindow->UnbindSafely();
        _trayIcon->Unbind();
    }
    else {
        emit _mainWindow->BindSafely();
        _trayIcon->Disconnect();
        _taskbarStatus->Disconnect();
    }

    // Queued rather than direct: this observer runs from `~ModifiableSafeAccessor`, which still
    // holds the settings mutex until it finishes. `OnBoundDeviceAddressChanged` joins the device
    // lookup thread, and `request_stop()` cannot interrupt the WinRT enumeration it is blocked
    // on, so calling it here would hold the settings mutex for the length of that enumeration
    // and stall every other thread touching settings.
    QMetaObject::invokeMethod(
        _airPodsManager.get(),
        [manager = _airPodsManager.get(), address] {
            manager->OnBoundDeviceAddressChanged(address);
        },
        Qt::QueuedConnection);
}

void ApbApplication::OnTrayIconBatteryChanged(Core::Settings::TrayIconBatteryBehavior behavior)
{
    emit _trayIcon->OnTrayIconBatteryChangedSafely(behavior);
}

void ApbApplication::OnTrayQuickConnectEnabledChanged(bool enable)
{
    _quickConnect->SetEnabled(enable);
}

void ApbApplication::OnTrayQuickConnectDeviceChanged(const QString &deviceId)
{
    _quickConnect->SetDeviceId(deviceId);
}

void ApbApplication::OnTaskbarBatteryChanged(Core::Settings::TaskbarStatusBehavior behavior)
{
    emit _taskbarStatus->OnSettingsChangedSafely(behavior);
}
