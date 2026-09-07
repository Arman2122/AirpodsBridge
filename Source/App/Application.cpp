// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/App/Application.h"

#include <QGuiApplication>
#include <QMessageBox>
#include <QTimer>

#include <Config.h>
#include "Source/Support/Logger.h"
#include "Source/Support/Error.h"
#include "Source/Support/Utils.h"
#include "Source/Gui/Utils.h"
#include "Source/Gui/DownloadWindow.h"
#include "Source/Gui/MainWindow.h"
#include "Source/Gui/TaskbarStatus.h"
#include "Source/Gui/FrameClock.h"
#include "Source/Gui/ThemeController.h"
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
#include "Source/Core/Update.h"
#include "Source/Presentation/Theme.h"

namespace {

// Long enough for the installer's window to be gone and the shell to have registered
// the tray icon, short enough to still read as a reply to the launch.
constexpr int kTrayWelcomeDelayMs = 1200;

} // namespace

ApbApplication::~ApbApplication()
{
    // Explicit rather than left to member order: no watcher may fire while the services
    // it drives are being torn down below.
    _settingsWatches.clear();
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

    // Every watcher registered by `WireSettings` sees its stored value exactly once here.
    Core::Settings::Apply();
}

void ApbApplication::FirstTimeUse()
{
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

    _trayWelcomePending = true;
}

void ApbApplication::ShowTrayWelcome()
{
    // Not shown from here: the event loop is not running yet, and a message shown before
    // the shell has the icon is lost.
    QTimer::singleShot(kTrayWelcomeDelayMs, this, [this] {
        _trayIcon->ShowMessage(
            tr("%1 is running").arg(Config::ProgramName),
            tr("You can find it in your system tray. Click the icon to see the battery, "
               "right-click for settings or to quit."));
    });
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

    // Light or dark, following Windows until the setting is replayed below; the windows
    // are built under the right palette either way, and nothing is on screen yet.
    _theme = std::make_unique<Gui::ThemeController>();
    // Every animation from here on advances once per frame of the display it is on.
    _frameClock = std::make_unique<Gui::FrameClock>();

    _trayIcon = std::make_unique<Gui::TrayIcon>(*_quickConnect);
    _taskbarStatus = std::make_unique<Gui::TaskbarStatus>();
    _mainWindow = std::make_unique<Gui::MainWindow>();
    _autoStartService = Core::AutoStart::CreateAutoStartService();
    _airPodsManager = std::make_unique<Core::AirPods::Manager>(this);
    // The base is private, so the conversion has to happen here, inside the class.
    auto &effects = static_cast<Core::Automation::Effects &>(*this);
    _automation = std::make_unique<Core::Automation::Engine>(effects, this);

    // The checker reports from its own thread; the answer is a dialog or a tray change, so
    // it is carried over to the GUI thread first.
    _updateChecker = std::make_unique<Core::Update::AsyncChecker>(
        [this](const Core::Update::ReleaseInfo &releaseInfo, bool silent) {
            QMetaObject::invokeMethod(
                this, [this, releaseInfo, silent] { OnUpdateAvailable(releaseInfo, silent); },
                Qt::QueuedConnection);
        });

    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");

    ConnectGuiComponents();
    ConnectAirPodsManager();
    WireSettings();

    _mainWindow->Unavailable();
    _trayIcon->Unavailable();
    _taskbarStatus->Unavailable();

    InitSettings(settingsLoadResult);

    // After the installer, and on the first run, the program says where it went. Anyone
    // who already has settings and starts it by hand knows.
    if (opts.afterInstall || _trayWelcomePending) {
        ShowTrayWelcome();
    }

    return true;
}

void ApbApplication::ConnectAirPodsManager()
{
    auto *manager = _airPodsManager.get();
    auto *mainWindow = _mainWindow.get();
    auto *automation = _automation.get();

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
    connect(
        manager, &Core::AirPods::Manager::LidToggled, mainWindow, [this, mainWindow](bool opened) {
            // Closing the lid always dismisses the popup; opening it is the user's choice.
            if (!opened) {
                emit mainWindow->HideSafely();
            }
            else if (_showPopupOnLidOpen) {
                emit mainWindow->ShowSafely();
            }
        });

    // The manager reports from its Bluetooth thread, under its own lock; the engine runs on
    // the GUI thread, so every report is queued across.
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, automation,
        &Core::Automation::Engine::OnStateUpdated, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, automation,
        &Core::Automation::Engine::OnDisconnected, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::DeviceConnectionChanged, automation,
        &Core::Automation::Engine::OnDeviceConnectionChanged, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::LidOpenedNearby, automation,
        &Core::Automation::Engine::OnLidOpenedNearby, Qt::QueuedConnection);
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

    // "Remind me later" means later: no further checks this session.
    connect(_mainWindow.get(), &Gui::MainWindow::UpdateDeferred, this, [this] {
        _updateChecker->Stop();
    });
}

//
// One line per setting. A field with no line here is stored and shown, but drives nothing.
//
void ApbApplication::WireSettings()
{
    using Core::Settings::Fields;
    using Core::Settings::Watch;

    auto *manager = _airPodsManager.get();
    auto *automation = _automation.get();

    _settingsWatches.clear();

    // Application
    _settingsWatches.push_back(
        Watch(&Fields::auto_run, [this](bool enable) { _autoStartService->SetEnabled(enable); }));
    _settingsWatches.push_back(
        Watch(&Fields::check_for_updates, [this](bool enable) { SetUpdateCheckEnabled(enable); }));
    _settingsWatches.push_back(Watch(
        &Fields::show_popup_on_lid_open, [this](bool enable) { _showPopupOnLidOpen = enable; }));
    _settingsWatches.push_back(Watch(&Fields::appearance, [this](Core::Settings::Appearance value) {
        _theme->SetAppearance(value);
    }));

    // Device
    _settingsWatches.push_back(Watch(
        &Fields::rssi_min, [manager](int16_t rssiMin) { manager->OnRssiMinChanged(rssiMin); }));
    _settingsWatches.push_back(Watch(&Fields::auto_connect_on_lid_open, [manager](bool enable) {
        manager->SetScanWhileDisconnected(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::device_address, [this, manager](uint64_t address) {
        if (address == 0) {
            emit _mainWindow->UnbindSafely();
            _trayIcon->Unbind();
        }
        else {
            emit _mainWindow->BindSafely();
            _trayIcon->Disconnect();
            _taskbarStatus->Disconnect();
        }

        // Queued: `OnBoundDeviceAddressChanged` joins the device lookup thread, which can be
        // blocked inside a WinRT enumeration, and the settings write that got us here should
        // return to its caller before that wait starts.
        QMetaObject::invokeMethod(
            manager, [manager, address] { manager->OnBoundDeviceAddressChanged(address); },
            Qt::QueuedConnection);
    }));

    // Automation
    _settingsWatches.push_back(Watch(&Fields::automatic_ear_detection, [automation](bool enable) {
        automation->SetEarDetectionEnabled(enable);
    }));
    _settingsWatches.push_back(
        Watch(&Fields::ear_detection_resume_playback, [automation](bool enable) {
            automation->SetEarDetectionResumePlayback(enable);
        }));
    _settingsWatches.push_back(Watch(&Fields::ear_detection_single_bud, [automation](bool enable) {
        automation->SetEarDetectionSingleBud(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::pause_on_disconnect, [automation](bool enable) {
        automation->SetPauseOnDisconnect(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::low_battery_notification, [automation](bool enable) {
        automation->SetLowBatteryNotification(enable);
    }));
    _settingsWatches.push_back(
        Watch(&Fields::low_battery_threshold, [this, automation](uint32_t threshold) {
            automation->SetLowBatteryThreshold(threshold);
            emit _trayIcon->OnLowBatteryThresholdChangedSafely(threshold);
        }));
    _settingsWatches.push_back(Watch(&Fields::auto_connect_on_lid_open, [automation](bool enable) {
        automation->SetAutoConnectOnLidOpen(enable);
    }));

    // Tray and taskbar
    _settingsWatches.push_back(
        Watch(&Fields::tray_icon_battery, [this](Core::Settings::TrayIconBatteryBehavior value) {
            emit _trayIcon->OnTrayIconBatteryChangedSafely(value);
        }));
    _settingsWatches.push_back(
        Watch(&Fields::battery_on_taskbar, [this](Core::Settings::TaskbarStatusBehavior value) {
            emit _taskbarStatus->OnSettingsChangedSafely(value);
        }));

    // Quick connect
    _settingsWatches.push_back(Watch(&Fields::tray_quick_connect_enabled, [this](bool enable) {
        _quickConnect->SetEnabled(enable);
    }));
    _settingsWatches.push_back(
        Watch(&Fields::tray_quick_connect_device_id, [this](const QString &deviceId) {
            _quickConnect->SetDeviceId(deviceId);
        }));
}

void ApbApplication::SetUpdateCheckEnabled(bool enable)
{
    // `AsyncChecker::Start` checks immediately, so re-applying an unchanged setting must
    // not restart it and hit GitHub again.
    if (enable == _updateCheckEnabled) {
        return;
    }
    _updateCheckEnabled = enable;

    if (enable) {
        _updateChecker->Start();
    }
    else {
        _updateChecker->Stop();
    }
}

void ApbApplication::OnUpdateAvailable(const Core::Update::ReleaseInfo &releaseInfo, bool silent)
{
    LOG(Info, "Update available. silent: {}", silent);

    if (silent) {
        _trayIcon->VersionUpdateAvailable(releaseInfo);
    }
    else {
        _mainWindow->AskUserUpdate(releaseInfo);
    }
}

//////////////////////////////////////////////////
// Core::Automation::Effects
//

void ApbApplication::PlayMedia()
{
    Core::GlobalMedia::Play();
}

void ApbApplication::PauseMedia()
{
    Core::GlobalMedia::Pause();
}

void ApbApplication::Notify(const QString &title, const QString &text)
{
    _trayIcon->ShowMessage(title, text);
}

void ApbApplication::ConnectDevice()
{
    _quickConnect->RequestAutomatic();
}

//////////////////////////////////////////////////

int ApbApplication::Run()
{
    _airPodsManager->StartScanner();
    return exec();
}

void ApbApplication::QuitSafely()
{
    Utils::Qt::QuitApplicationSafely();
}
