// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/App/Application.h"

#include <QGuiApplication>
#include <QMessageBox>
#include <QTimer>

#include <nlohmann/json.hpp>

#include <Config.h>
#include "Source/Core/AirPods.h"
#if defined APB_OS_WIN
    #include "Source/Core/AirPodsServiceTransport_win.h"
    #include "Source/Core/AirPodsTransport_win.h"
#endif
#include "Source/Core/AudioControl.h"
#include "Source/Core/AutoStart.h"
#include "Source/Core/Bluetooth.h"
#include "Source/Core/GlobalHotkeys.h"
#include "Source/Core/GlobalMedia.h"
#include "Source/Core/Ipc.h"
#include "Source/Core/Notifications.h"
#include "Source/Core/QuickConnect.h"
#if defined APB_OS_WIN
    #include "Source/Core/QuickConnect_win.h"
#endif
#include "Source/Core/Settings.h"
#include "Source/Core/SystemEvents.h"
#include "Source/Core/Update.h"
#include "Source/Gui/DownloadWindow.h"
#include "Source/Gui/FrameClock.h"
#include "Source/Gui/MainWindow.h"
#include "Source/Gui/TaskbarStatus.h"
#include "Source/Gui/ThemeController.h"
#include "Source/Gui/TrayIcon.h"
#include "Source/Gui/Utils.h"
#include "Source/Gui/WelcomeDialog.h"
#include "Source/Presentation/Theme.h"
#include "Source/Support/Error.h"
#include "Source/Support/Logger.h"
#include "Source/Support/Utils.h"

namespace {

// Long enough for the installer's window to be gone and the shell to have registered
// the tray icon, short enough to still read as a reply to the launch.
constexpr int kTrayWelcomeDelayMs = 1200;

} // namespace

ApbApplication::~ApbApplication()
{
    // Explicit rather than left to member order: no watcher, IPC command, hotkey, audio
    // callback, or system broadcast may fire while the services they drive are being torn
    // down below, and the global media worker must join before CRT/WinRT teardown.
    _settingsWatches.clear();
    _ipcServer.reset();
    _hotkeys.reset();
    _systemEvents.reset();
    _audioControl.reset();
    Core::GlobalMedia::Shutdown();
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
    _firstTimeSetupRan = true;
    bool serviceAlreadyRunning = false;
#if defined APB_OS_WIN
    serviceAlreadyRunning = Core::AirPods::ServiceControlTransport::IsServiceRunning();
#endif

    Gui::WelcomeDialog welcome{serviceAlreadyRunning};
    welcome.exec();
    const auto res = welcome.GetResult();

    auto current = Core::Settings::GetCurrent();
    current.auto_run = res.autoRun;
    current.show_nearby_popup_when_disconnected = res.showNearbyPopup;
    current.auto_enable_radio_service = res.controlEngineEnabled;
    Core::Settings::Save(std::move(current));

    if (res.controlEngineEnabled) {
        _airPodsManager->ReconnectControlTransport();
        _trayIcon->NotifyRadioControlChanged();
    }

    if (res.rebootRequired) {
        QMessageBox::information(
            nullptr, Config::ProgramName,
            tr("The Control Engine was installed.\n\n"
               "Restart Windows once so the driver finishes initializing."));
    }

    _trayWelcomePending = true;
}

void ApbApplication::EnsureRadioServiceEnabledOnStart()
{
#if defined APB_OS_WIN
    if (!_autoEnableRadioService || _autoEnableRadioAttempted) {
        return;
    }
    _autoEnableRadioAttempted = true;

    const auto snap = _airPodsManager->GetDiagnostics();
    if (snap.radioServiceInfo.has_value() && snap.radioServiceInfo->guardTripped) {
        LOG(Info, "Control engine safety guard tripped; skipping automatic start.");
        return;
    }

    if (Core::AirPods::ServiceControlTransport::IsServiceRunning()) {
        _airPodsManager->ReconnectControlTransport();
        _trayIcon->NotifyRadioControlChanged();
        return;
    }

    LOG(Info, "Control engine is not running and auto-enable is on; starting setup.");
    _trayIcon->SetRadioSetupBusy(true, tr("Starting control engine..."));
    Core::AirPods::ServiceControlTransport::RunElevatedSetupAsync(
        false, false,
        [this](Core::AirPods::ServiceControlTransport::SetupOutcome outcome) {
            QMetaObject::invokeMethod(
                this,
                [this, outcome = std::move(outcome)]() {
                    if (outcome.succeeded) {
                        _quickConnect->InvalidateCache();
                        _trayIcon->SetRadioSetupBusy(false);
                        const auto reconcile = [this] {
                            _quickConnect->InvalidateCache();
                            _quickConnect->RefreshDevices();
                            _airPodsManager->RebindAfterRadioChange();
                            const auto f = Core::Settings::GetCurrent();
                            _airPodsManager->ApplyRadioProxyOptions(
                                Core::AirPods::RadioProxyOptions{
                                    .lowLatencyQos = f.radio_service_low_latency_qos,
                                    .pollLatencyUs = 25000,
                                    .declineHfp = f.radio_service_hfp_decline,
                                    .virtualMicEnabled = f.radio_service_virtual_mic,
                                });
                            _airPodsManager->ReconnectControlTransport();
                            _trayIcon->NotifyRadioControlChanged();
                        };
                        QTimer::singleShot(2500, this, reconcile);
                        QTimer::singleShot(5500, this, [this] {
                            _airPodsManager->ReconnectControlTransport();
                            _trayIcon->NotifyRadioControlChanged();
                        });
                    }
                    else {
                        _trayIcon->SetRadioSetupBusy(false);
                        LOG(Info, "Auto-enable control engine did not complete: {}",
                            outcome.message);
                    }
                },
                Qt::QueuedConnection);
        },
        [this](int percent, std::string message) {
            QMetaObject::invokeMethod(
                this,
                [this, percent, msg = QString::fromStdString(message)]() {
                    _trayIcon->SetRadioSetupProgress(percent, msg);
                },
                Qt::QueuedConnection);
        });
#endif
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
    if (Utils::File::IsPortableMode()) {
        LOG(Info, "Running in portable mode.");
    }

    // Registers the bundled faces and resolves the family, so every widget constructed
    // after this point inherits the interface font. See Source/Presentation/Theme.h.
    Gui::Theme::Font::Register();
    setFont(Gui::Theme::Font::Body());
    setWindowIcon(QIcon{Config::QrcIconSvg});
    setQuitOnLastWindowClosed(false);

#if defined APB_OS_WIN
    Support::OS::Windows::Winrt::Initialize();
    Core::Notifications::Initialize();
#endif

    // Loaded before the GUI is constructed: `InitSettings` replays the result once the
    // windows exist, and the widgets read the already-loaded values while they are built.
    const auto settingsLoadResult = Core::Settings::Load();

#if defined APB_OS_WIN
    _quickConnectBackend = std::make_shared<Core::QuickConnect::WindowsBackend>();
    std::unique_ptr<Core::AirPods::ControlTransport> aapTransport =
        std::make_unique<Core::AirPods::CompositeControlTransport>(
            std::make_unique<Core::AirPods::ServiceControlTransport>(),
            std::make_unique<Core::AirPods::L2capControlTransport>());
#else
    _quickConnectBackend = std::make_shared<Core::QuickConnect::NullBackend>();
    std::unique_ptr<Core::AirPods::ControlTransport> aapTransport =
        std::make_unique<Core::AirPods::NullControlTransport>();
#endif
    _quickConnect = std::make_unique<Core::QuickConnect::Controller>(_quickConnectBackend);

    // Light or dark, following Windows until the setting is replayed below; the windows
    // are built under the right palette either way, and nothing is on screen yet.
    _theme = std::make_unique<Gui::ThemeController>();
    // Every animation from here on advances once per frame of the display it is on.
    _frameClock = std::make_unique<Gui::FrameClock>();

    _trayIcon = std::make_unique<Gui::TrayIcon>(*_quickConnect);
    _taskbarStatus = std::make_unique<Gui::TaskbarStatus>();
    _taskbarStatus->SetTaskbarDark(_theme->IsTaskbarDark());
    _mainWindow = std::make_unique<Gui::MainWindow>();
    _autoStartService = Core::AutoStart::CreateAutoStartService();
    _airPodsManager = std::make_unique<Core::AirPods::Manager>(this, std::move(aapTransport));
    _audioControl = std::make_unique<Core::AudioControl::Service>(this);
    _hotkeys = std::make_unique<Core::GlobalHotkeys::Manager>(this);
    // The base is private, so the conversion has to happen here, inside the class.
    auto &effects = static_cast<Core::Automation::Effects &>(*this);
    _automation =
        std::make_unique<Core::Automation::Engine>(effects, this, std::chrono::milliseconds{1200});

    // The checker reports from its own thread; the answer is a dialog or a tray change, so
    // it is carried over to the GUI thread first.
    _updateChecker = std::make_unique<Core::Update::AsyncChecker>(
        [this](const Core::Update::ReleaseInfo &releaseInfo, bool silent) {
            QMetaObject::invokeMethod(
                this, [this, releaseInfo, silent] { OnUpdateAvailable(releaseInfo, silent); },
                Qt::QueuedConnection);
        });

    _systemEvents =
        std::make_unique<Core::SystemEvents::Watcher>(Core::SystemEvents::Watcher::Callbacks{
            .onSuspend = [this] { _airPodsManager->OnSystemSuspend(); },
            .onResume = [this] { _airPodsManager->OnSystemResume(); },
            .onTaskbarCreated = [this] { _taskbarStatus->OnTaskbarCreated(); },
        });

    qRegisterMetaType<Core::AirPods::State>("Core::AirPods::State");
    qRegisterMetaType<Core::AirPods::Model>("Core::AirPods::Model");
    qRegisterMetaType<Core::AirPods::NoiseControlMode>("Core::AirPods::NoiseControlMode");
    qRegisterMetaType<Core::AirPods::AudioProfile>("Core::AirPods::AudioProfile");
    qRegisterMetaType<Core::GlobalHotkeys::Action>("Core::GlobalHotkeys::Action");

    ConnectGuiComponents();
    ConnectAirPodsManager();
    WireSettings();

    _ipcServer = std::make_unique<Core::Ipc::Server>(
        [this](const QByteArray &req) { return HandleIpcRequest(req); });
    _ipcServer->Start();

    // Launching a second copy while AirpodsBridge is already in the tray surfaces the popup
    // instead of exiting silently.
    connect(
        this, &SingleApplication::instanceStarted, this, [this] { _mainWindow->ShowPopup(true); });

    _mainWindow->Unavailable();
    _trayIcon->Unavailable();
    _taskbarStatus->Unavailable();

    InitSettings(settingsLoadResult);

    if (!_firstTimeSetupRan && _autoEnableRadioService) {
        QTimer::singleShot(700, this, [this] { EnsureRadioServiceEnabledOnStart(); });
    }

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
            settings->bound_device_name.clear();
        },
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::BoundDeviceResolved, this,
        [this](const QString &displayName, Core::AirPods::Model model, const QString &containerId) {
            _mainWindow->SetBoundDevice(model, displayName);
            _audioControl->SetTrackedContainerId(containerId);

            auto settings = Core::Settings::ModifiableAccess();
            if (!displayName.isEmpty() && settings->bound_device_name != displayName) {
                settings->bound_device_name = displayName;
            }
            if (settings->tray_quick_connect_device_id.isEmpty() && !containerId.isEmpty()) {
                settings->tray_quick_connect_device_id = containerId;
            }
        },
        Qt::QueuedConnection);
    connect(
        _audioControl.get(), &Core::AudioControl::Service::AudioProfileChanged, this,
        [this](Core::AirPods::AudioProfile profile, const QString &sourceProcess) {
            _airPodsManager->SetAudioProfile(profile, sourceProcess);
        });
    connect(
        _audioControl.get(), &Core::AudioControl::Service::EndpointsChanged, _trayIcon.get(),
        &Gui::TrayIcon::NotifyAudioEndpointsChanged);
    connect(
        _audioControl.get(), &Core::AudioControl::Service::LatencyDiagnosticsChanged,
        _trayIcon.get(), &Gui::TrayIcon::NotifyLatencyDiagnosticsChanged);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, mainWindow, &Gui::MainWindow::UpdateState,
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, this,
        [this, wasOneBudInEar = false](const Core::AirPods::State &state) mutable {
            _audioControl->SetAirPodsWorn(
                Core::Automation::EarDetectionPolicy::IsWorn(state, true));
            if (!Core::AirPods::IsOnePiece(state.model)) {
                const bool leftIn = state.pods.left.isInEar;
                const bool rightIn = state.pods.right.isInEar;
                const bool bothIn = leftIn && rightIn;
                const bool oneIn = leftIn != rightIn;
                if (wasOneBudInEar && bothIn) {
                    _audioControl->NotifyBudReinserted();
                }
                wasOneBudInEar = oneIn;
            }
            else {
                wasOneBudInEar = false;
            }
        },
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, _trayIcon.get(),
        &Gui::TrayIcon::UpdateState, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::StateUpdated, _taskbarStatus.get(),
        &Gui::TaskbarStatus::UpdateState, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, mainWindow, &Gui::MainWindow::Disconnect,
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, this,
        [this] { _audioControl->SetAirPodsWorn(false); }, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, _trayIcon.get(), &Gui::TrayIcon::Disconnect,
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::Disconnected, _taskbarStatus.get(),
        &Gui::TaskbarStatus::Disconnect, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::ScannerAvailabilityChanged, mainWindow,
        [this, mainWindow](bool available) {
            const auto snap = _airPodsManager->GetDiagnostics();
            if (snap.aapConnected || snap.bluetoothConnected) {
                // Never overwrite a live connected AirPods session when the BLE scanner starts
                // or aborts during a radio driver reload.
                if (available) {
                    mainWindow->Available();
                }
                return;
            }
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
        },
        Qt::QueuedConnection);
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

    // The manager reports from its Bluetooth thread, under its own lock; the engine and tray
    // live on the GUI thread, so every report is queued across.
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
        manager, &Core::AirPods::Manager::DeviceConnectionChanged, _trayIcon.get(),
        &Gui::TrayIcon::SetDeviceConnected, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::DeviceConnectionChanged, this,
        [this, wasConnected = false](bool connected) mutable {
            const bool justDisconnected = wasConnected && !connected;
            wasConnected = connected;
            _quickConnect->NotifyBluetoothConnected(connected);
            _audioControl->SetBluetoothConnected(connected);
            _mainWindow->SetConnecting(false);
            if (!connected) {
                _audioControl->SetAirPodsWorn(false);
                if (justDisconnected && Core::Settings::GetCurrent().auto_switch_audio_on_connect) {
                    _audioControl->RestorePreviousOutput();
                }
            }
            else if (Core::Settings::GetCurrent().auto_switch_audio_on_connect) {
                _audioControl->RequestSwitchOutputToAirPods();
            }
        },
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::ControlConnectionChanged, this,
        [this](bool connected) {
            if (!connected) {
                _audioControl->RestoreConversationDucking();
            }
            _trayIcon->NotifyRadioControlChanged();
        },
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::ConversationAwarenessLevelChanged, this,
        [this](uint8_t level) { _audioControl->OnConversationAwarenessLevel(level); },
        Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::LidOpenedNearby, automation,
        &Core::Automation::Engine::OnLidOpenedNearby, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::NearbyStateUpdated, mainWindow,
        &Gui::MainWindow::UpdateNearbyState, Qt::QueuedConnection);
    connect(
        manager, &Core::AirPods::Manager::NearbyCleared, mainWindow, &Gui::MainWindow::ClearNearby,
        Qt::QueuedConnection);
}

void ApbApplication::ConnectGuiComponents()
{
    connect(
        _theme.get(), &Gui::ThemeController::TaskbarModeChanged, _taskbarStatus.get(),
        &Gui::TaskbarStatus::SetTaskbarDark);

    connect(_trayIcon.get(), &Gui::TrayIcon::ShowMainWindowRequested, this, [this] {
        _mainWindow->ShowPopup(true);
    });
    connect(
        _trayIcon.get(), &Gui::TrayIcon::UserUpdateRequested, _mainWindow.get(),
        &Gui::MainWindow::AskUserUpdate);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::ToolTipChanged, _taskbarStatus.get(),
        &Gui::TaskbarStatus::SetToolTip);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::BindDeviceRequested, _mainWindow.get(),
        &Gui::MainWindow::BindDevice);
    connect(_trayIcon.get(), &Gui::TrayIcon::SwitchAudioOutputRequested, this, [this] {
        _audioControl->SwitchOutputToAirPods();
    });
    connect(
        _trayIcon.get(), &Gui::TrayIcon::NoiseControlModeRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetNoiseControlMode);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::ConversationalAwarenessRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetConversationalAwareness);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::AdaptiveNoiseLevelRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetAdaptiveNoiseLevel);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::OneBudAncRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetOneBudAnc);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::PersonalizedVolumeRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetPersonalizedVolume);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::LoudSoundReductionRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetLoudSoundReduction);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::HeadGesturesRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetHeadGestures);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::MicrophoneSideRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetMicrophoneSide);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::PressSpeedRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetPressSpeed);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::PressHoldDurationRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetPressHoldDuration);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::ToneVolumeRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetToneVolume);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::AllowOffListeningModeRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetAllowOffListeningMode);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::HearingAidRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetHearingAid);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::MediaAssistRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetMediaAssist);
    connect(
        _trayIcon.get(), &Gui::TrayIcon::HeartRateMonitoringRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetHeartRateMonitoring);

    connect(
        _trayIcon.get(), &Gui::TrayIcon::RadioServiceActionRequested, this,
        [this](Gui::SettingsWindow::RadioServiceAction action) {
            using Action = Gui::SettingsWindow::RadioServiceAction;
            if (action == Action::RetryGuard) {
                _airPodsManager->RetryRadioServiceAfterGuard();
                _trayIcon->SetRadioSetupBusy(false);
                return;
            }
            if (action == Action::RestoreNative && _airPodsManager->RestoreNativeRadioDriver()) {
                _airPodsManager->DisconnectControlTransport();
                _quickConnect->InvalidateCache();
                _trayIcon->SetRadioSetupBusy(
                    false, tr("Native Windows Bluetooth driver restored."));
                QTimer::singleShot(2500, this, [this] {
                    _airPodsManager->RebindAfterRadioChange();
                    _quickConnect->RefreshDevices();
                    _trayIcon->NotifyRadioControlChanged();
                });
                return;
            }
#if defined APB_OS_WIN
            const bool uninstall = (action == Action::Uninstall);
            const bool restoreOnly = (action == Action::RestoreNative);
            Core::AirPods::ServiceControlTransport::RunElevatedSetupAsync(
                uninstall, restoreOnly,
                [this, uninstall,
                 restoreOnly](Core::AirPods::ServiceControlTransport::SetupOutcome outcome) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, uninstall, restoreOnly, outcome = std::move(outcome)]() {
                            const QString msg = QString::fromStdString(outcome.message);
                            if (outcome.succeeded) {
                                _quickConnect->InvalidateCache();
                                if (uninstall || restoreOnly) {
                                    _airPodsManager->DisconnectControlTransport();
                                }
                                _trayIcon->SetRadioSetupBusy(false, msg);
                                // Wait for Windows to finish enumerating the newly attached
                                // virtual or restored native Bluetooth adapter before rebinding
                                // WinRT device handles and reconnecting AAP.
                                const auto reconcile = [this, uninstall, restoreOnly] {
                                    _quickConnect->InvalidateCache();
                                    _quickConnect->RefreshDevices();
                                    _airPodsManager->RebindAfterRadioChange();
                                    if (!uninstall && !restoreOnly) {
                                        const auto f = Core::Settings::GetCurrent();
                                        _airPodsManager->ApplyRadioProxyOptions(
                                            Core::AirPods::RadioProxyOptions{
                                                .lowLatencyQos = f.radio_service_low_latency_qos,
                                                .pollLatencyUs = 25000,
                                                .declineHfp = f.radio_service_hfp_decline,
                                                .virtualMicEnabled = f.radio_service_virtual_mic,
                                            });
                                        _airPodsManager->ReconnectControlTransport();
                                    }
                                    _trayIcon->NotifyRadioControlChanged();
                                };
                                QTimer::singleShot(2500, this, reconcile);
                                QTimer::singleShot(5500, this, [this, uninstall, restoreOnly] {
                                    if (!uninstall && !restoreOnly) {
                                        _airPodsManager->ReconnectControlTransport();
                                    }
                                    _trayIcon->NotifyRadioControlChanged();
                                });
                                if (outcome.rebootRequired) {
                                    QMessageBox::information(
                                        nullptr, Config::ProgramName,
                                        tr("The No-Test-Mode Control Engine was installed.\n\n"
                                           "Restart Windows once so the signed usbip-win2 driver "
                                           "finishes initializing."));
                                }
                            }
                            else {
                                _trayIcon->SetRadioSetupBusy(false, msg);
                                if (!outcome.cancelled) {
                                    QMessageBox::warning(
                                        nullptr, Config::ProgramName,
                                        tr("Control Engine setup did not complete:\n\n%1")
                                            .arg(msg));
                                }
                            }
                        },
                        Qt::QueuedConnection);
                },
                [this](int percent, std::string message) {
                    QMetaObject::invokeMethod(
                        this,
                        [this, percent, msg = QString::fromStdString(message)]() {
                            _trayIcon->SetRadioSetupProgress(percent, msg);
                        },
                        Qt::QueuedConnection);
                });
#else
            _trayIcon->SetRadioSetupBusy(false);
#endif
        });

    _trayIcon->SetRadioControlProvider([this] {
        const auto snap = _airPodsManager->GetDiagnostics();
        Gui::SettingsWindow::RadioControlInfo info;
        info.aapConnected = snap.aapConnected;
        info.heartRateServiceId = snap.heartRateServiceId;
        info.state = snap.currentState;
        if (info.aapConnected && !info.state.has_value() &&
            snap.boundModel != Core::AirPods::Model::Unknown)
        {
            Core::AirPods::State fallback{};
            fallback.model = snap.boundModel;
            fallback.noiseControl.controllable = true;
            info.state = fallback;
        }
        if (snap.radioServiceInfo.has_value()) {
            const auto &rs = snap.radioServiceInfo.value();
            info.serviceInstalled = rs.serviceInstalled;
            info.serviceRunning = rs.serviceRunning;
            info.guardTripped = rs.guardTripped;
            if (rs.guardTripped) {
                info.engineDetail = QStringLiteral("%1 %2 %3")
                                        .arg(tr("Paused after repeated errors"))
                                        .arg(QChar{0x00B7})
                                        .arg(tr("Native driver restored"));
            }
            else if (rs.serviceRunning && rs.virtualAdapterAttached) {
                info.engineDetail =
                    QStringLiteral("%1 %2 %3")
                        .arg(tr("Active"))
                        .arg(QChar{0x00B7})
                        .arg(rs.activeAapChannels > 0 ? tr("Connected") : tr("Ready"));
            }
            else if (rs.serviceRunning) {
                info.engineDetail = tr("Starting...");
            }
            else if (rs.serviceInstalled) {
                info.engineDetail = tr("Stopped");
            }
            else {
                info.engineDetail =
                    QStringLiteral("%1 %2 %3")
                        .arg(tr("Off"))
                        .arg(QChar{0x00B7})
                        .arg(tr("Enables noise control, 1% battery, and heart rate."));
            }

            if (!rs.adapterDisplayName.empty()) {
                info.adapterSummary = QStringLiteral("%1 %2 %3")
                                          .arg(QString::fromStdString(rs.adapterDisplayName))
                                          .arg(QChar{0x00B7})
                                          .arg(rs.winUsbBound ? tr("Active") : tr("Native driver"));
            }
            else {
                info.adapterSummary = tr("Standard Windows driver");
            }
        }
        return info;
    });

    _trayIcon->SetAudioEndpointsProvider([this] { return _audioControl->ListCaptureEndpoints(); });
    _trayIcon->SetLatencyDiagnosticsProvider(
        [this] { return _audioControl->GetLatencyDiagnostics(); });
    connect(
        _trayIcon.get(), &Gui::TrayIcon::LatencyProfileSwitchRequested, this,
        [this](bool lowLatencySbc) {
            if (!_audioControl->ApplyLatencyProfileElevated(lowLatencySbc)) {
                _trayIcon->NotifyLatencyDiagnosticsChanged();
            }
        });
    connect(
        _audioControl.get(), &Core::AudioControl::Service::LatencyProfileApplied, this,
        [this](bool, bool a2dpRenegotiated) {
            if (!a2dpRenegotiated && _airPodsManager->GetDiagnostics().bluetoothConnected &&
                _quickConnect->HasDevice())
            {
                // Fallback when the PnP A2DP node could not be cycled in-process (e.g. WOW64 /
                // ARM64 emulation): drain the WASAPI stream cleanly first, then run a single
                // serialized Bluetooth reconnect pulse so overlapping toggles never collide.
                const uint64_t epoch = ++_codecReconnectEpoch;
                LOG(Info, "AudioControl: draining stream and pulsing native Bluetooth reconnect "
                          "to apply new codec.");
                _audioControl->SuspendKeepAliveTemporarily();
                QTimer::singleShot(150, this, [this, epoch] {
                    if (_codecReconnectEpoch != epoch) {
                        return;
                    }
                    _quickConnect->RequestDisconnect();
                    QTimer::singleShot(1600, this, [this, epoch] {
                        if (_codecReconnectEpoch != epoch) {
                            return;
                        }
                        _quickConnect->RequestConnect();
                    });
                });
            }
        });
    _trayIcon->SetDiagnosticsProvider([this] {
        const auto snap = _airPodsManager->GetDiagnostics();
        const auto lat = _audioControl->GetLatencyDiagnostics();
        Gui::SettingsWindow::DiagnosticsInfo info;
        if (snap.aapConnected && snap.transportBackend == QStringLiteral("radio_service")) {
            info.transportSummary =
                tr("AAP L2CAP connected via No-Test-Mode Radio Service (WinUSB + usbip-win2).");
        }
        else if (snap.aapConnected) {
            info.transportSummary =
                tr("AAP L2CAP connected via KMDF driver (1% battery & two-way control active).");
        }
        else if (
            snap.aapDriverAvailable && snap.transportBackend == QStringLiteral("radio_service"))
        {
            info.transportSummary =
                tr("BLE Broadcast (No-Test-Mode Radio Service ready, waiting for AirPods).");
        }
        else if (snap.aapDriverAvailable) {
            info.transportSummary =
                tr("BLE Broadcast (AAP L2CAP driver installed, waiting for AirPods).");
        }
        else {
            info.transportSummary =
                tr("BLE Broadcast (enable the No-Test-Mode Control Engine in AirPods control).");
        }

        QStringList connParts;
        if (!snap.boundDeviceName.isEmpty()) {
            connParts << snap.boundDeviceName;
        }
        connParts << (snap.bluetoothConnected ? tr("Connected") : tr("Disconnected"));
        const auto profile = _audioControl->CurrentProfile();
        if (profile == Core::AirPods::AudioProfile::HandsFree) {
            const auto proc = _audioControl->CurrentHfpProcess();
            connParts << (proc.isEmpty() ? tr("Hands-Free") : tr("Hands-Free (%1)").arg(proc));
        }
        else if (profile == Core::AirPods::AudioProfile::Stereo) {
            connParts << tr("A2DP Stereo (%1)").arg(lat.codec);
        }
        if (lat.keepAliveActive) {
            connParts << tr("Stream awake");
        }
        if (snap.lastRssi.has_value()) {
            connParts << QStringLiteral("%1 dBm").arg(snap.lastRssi.value());
        }
        info.connectionSummary = connParts.join(QStringLiteral(" \u00B7 "));
        info.clipboardReport = BuildStatusJson();
        return info;
    });

    connect(
        _hotkeys.get(), &Core::GlobalHotkeys::Manager::Triggered, this,
        [this](Core::GlobalHotkeys::Action action) {
            switch (action) {
            case Core::GlobalHotkeys::Action::ToggleConnect:
                _quickConnect->RequestToggle();
                break;
            case Core::GlobalHotkeys::Action::TogglePopup:
                _mainWindow->ToggleVisibility();
                break;
            case Core::GlobalHotkeys::Action::CycleNoiseControl:
                _airPodsManager->CycleNoiseControlMode();
                break;
            }
        });
    connect(_trayIcon.get(), &Gui::TrayIcon::ManualUpdateCheckRequested, this, [this] {
        _updateChecker->CheckNow([this](const Core::Update::CheckResult &result) {
            QMetaObject::invokeMethod(
                this, [this, result] { OnManualUpdateCheckCompleted(result); },
                Qt::QueuedConnection);
        });
    });

    connect(
        _quickConnect.get(), &Core::QuickConnect::Controller::OutcomeChanged, this,
        [this](Core::QuickConnect::Outcome outcome, const QString &) {
            if (outcome == Core::QuickConnect::Outcome::RequestStarted) {
                _mainWindow->SetConnecting(true);
            }
            else {
                _mainWindow->SetConnecting(false);
            }
            if (outcome == Core::QuickConnect::Outcome::TimedOut) {
                _mainWindow->SetTransientStatus(tr("Open case or bring AirPods closer to connect"));
            }
            else if (
                outcome == Core::QuickConnect::Outcome::Failed ||
                outcome == Core::QuickConnect::Outcome::DeviceUnavailable)
            {
                _mainWindow->SetTransientStatus(tr("AirPods not reachable right now"));
            }
            if ((outcome == Core::QuickConnect::Outcome::Connected ||
                 outcome == Core::QuickConnect::Outcome::AlreadyConnected) &&
                Core::Settings::GetCurrent().auto_switch_audio_on_connect)
            {
                _audioControl->RequestSwitchOutputToAirPods();
            }
        });
    connect(_mainWindow.get(), &Gui::MainWindow::QuickConnectRequested, this, [this] {
        _mainWindow->SetConnecting(true);
        if (Core::Settings::GetCurrent().auto_switch_audio_on_connect) {
            _audioControl->RequestSwitchOutputToAirPods();
        }
        _quickConnect->RequestConnect();
    });
    connect(
        _mainWindow.get(), &Gui::MainWindow::NoiseControlModeRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetNoiseControlMode);
    connect(
        _mainWindow.get(), &Gui::MainWindow::AdaptiveNoiseLevelRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetAdaptiveNoiseLevel);
    connect(
        _mainWindow.get(), &Gui::MainWindow::ConversationalAwarenessRequested,
        _airPodsManager.get(), &Core::AirPods::Manager::SetConversationalAwareness);
    connect(
        _mainWindow.get(), &Gui::MainWindow::PersonalizedVolumeRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetPersonalizedVolume);
    connect(
        _mainWindow.get(), &Gui::MainWindow::HearingAidRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetHearingAid);
    connect(
        _mainWindow.get(), &Gui::MainWindow::HeartRateMonitoringRequested, _airPodsManager.get(),
        &Core::AirPods::Manager::SetHeartRateMonitoring);
    connect(_mainWindow.get(), &Gui::MainWindow::OpenControlSettingsRequested, this, [this] {
        _trayIcon->OpenSettings(Gui::SettingsWindow::Section::Control);
    });
    connect(_mainWindow.get(), &Gui::MainWindow::OpenHealthSettingsRequested, this, [this] {
        _trayIcon->OpenSettings(Gui::SettingsWindow::Section::Health);
    });

    connect(_taskbarStatus.get(), &Gui::TaskbarStatus::ShowMainWindowRequested, this, [this] {
        _mainWindow->ShowPopup(true);
    });
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
    _settingsWatches.push_back(Watch(&Fields::show_popup_on_lid_open, [this, manager](bool enable) {
        _showPopupOnLidOpen = enable;
        manager->SetShowPopupOnLidOpen(enable);
    }));
    _settingsWatches.push_back(
        Watch(&Fields::show_nearby_popup_when_disconnected, [manager](bool enable) {
            manager->SetShowNearbyWhenDisconnected(enable);
        }));
    _settingsWatches.push_back(Watch(&Fields::show_heart_rate_in_popup, [this](bool enable) {
        _mainWindow->SetShowHeartRateInPopup(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::appearance, [this](Core::Settings::Appearance value) {
        _theme->SetAppearance(value);
    }));

    // Device
    _settingsWatches.push_back(Watch(
        &Fields::rssi_min, [manager](int16_t rssiMin) { manager->OnRssiMinChanged(rssiMin); }));
    _settingsWatches.push_back(Watch(&Fields::auto_connect_on_lid_open, [manager](bool enable) {
        manager->SetScanWhileDisconnected(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::bound_device_name, [this](const QString &) {
        QMetaObject::invokeMethod(
            _trayIcon.get(), [this] { _trayIcon->RefreshSettings(); }, Qt::QueuedConnection);
    }));
    _settingsWatches.push_back(Watch(&Fields::device_address, [this, manager](uint64_t address) {
        if (address == 0) {
            emit _mainWindow->UnbindSafely();
            _trayIcon->Unbind();
            _audioControl->SetTrackedContainerId({});
        }
        else {
            emit _mainWindow->BindSafely();
            _trayIcon->Disconnect();
            _taskbarStatus->Disconnect();
        }
        _trayIcon->RefreshSettings();

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
            QMetaObject::invokeMethod(
                _mainWindow.get(),
                [this, threshold] { _mainWindow->SetLowBatteryThreshold(threshold); },
                Qt::QueuedConnection);
        }));
    _settingsWatches.push_back(Watch(&Fields::auto_connect_on_lid_open, [automation](bool enable) {
        automation->SetAutoConnectOnLidOpen(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::full_charge_notification, [automation](bool enable) {
        automation->SetFullChargeNotification(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::smart_audio_routing, [automation](bool enable) {
        automation->SetSmartAudioRouting(enable);
    }));
    _settingsWatches.push_back(Watch(&Fields::native_toast_notifications, [this](bool enable) {
        _nativeToastNotifications = enable;
    }));

    // Audio protection
    _settingsWatches.push_back(Watch(&Fields::auto_switch_audio_on_connect, [this](bool enable) {
        _audioControl->SetAutoSwitchOnConnect(enable);
    }));
    _settingsWatches.push_back(
        Watch(&Fields::preferred_mic_device_id, [this](const QString &endpointId) {
            _audioControl->SetPreferredMicrophoneId(endpointId);
        }));
    _settingsWatches.push_back(Watch(&Fields::volume_cap_enabled, [this](bool enable) {
        _audioControl->SetVolumeCap(enable, Core::Settings::GetCurrent().volume_cap_level);
    }));
    _settingsWatches.push_back(Watch(&Fields::volume_cap_level, [this](uint32_t level) {
        _audioControl->SetVolumeCap(Core::Settings::GetCurrent().volume_cap_enabled, level);
    }));
    _settingsWatches.push_back(Watch(&Fields::audio_keep_alive, [this, manager](bool enable) {
        _audioControl->SetKeepAliveEnabled(enable);
        manager->SetLowLatencyMode(enable);
    }));

    // No-Test-Mode Radio Service proxy options
    _settingsWatches.push_back(Watch(&Fields::auto_enable_radio_service, [this](bool enable) {
        _autoEnableRadioService = enable;
    }));
    auto pushRadioOptions = [manager] {
        const auto f = Core::Settings::GetCurrent();
        manager->ApplyRadioProxyOptions(Core::AirPods::RadioProxyOptions{
            .lowLatencyQos = f.radio_service_low_latency_qos,
            .pollLatencyUs = 25000,
            .declineHfp = f.radio_service_hfp_decline,
            .virtualMicEnabled = f.radio_service_virtual_mic,
        });
    };
    _settingsWatches.push_back(Watch(
        &Fields::radio_service_low_latency_qos, [pushRadioOptions](bool) { pushRadioOptions(); }));
    _settingsWatches.push_back(Watch(
        &Fields::radio_service_hfp_decline, [pushRadioOptions](bool) { pushRadioOptions(); }));
    _settingsWatches.push_back(Watch(
        &Fields::radio_service_virtual_mic, [pushRadioOptions](bool) { pushRadioOptions(); }));

    // Global shortcuts
    _settingsWatches.push_back(Watch(&Fields::hotkey_toggle_connect, [this](const QString &spec) {
        _hotkeys->SetBinding(Core::GlobalHotkeys::Action::ToggleConnect, spec);
    }));
    _settingsWatches.push_back(Watch(&Fields::hotkey_toggle_popup, [this](const QString &spec) {
        _hotkeys->SetBinding(Core::GlobalHotkeys::Action::TogglePopup, spec);
    }));
    _settingsWatches.push_back(
        Watch(&Fields::hotkey_cycle_noise_control, [this](const QString &spec) {
            _hotkeys->SetBinding(Core::GlobalHotkeys::Action::CycleNoiseControl, spec);
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
            const bool hasDevice = !deviceId.trimmed().isEmpty();
            QMetaObject::invokeMethod(
                this,
                [this, hasDevice] {
                    _mainWindow->SetQuickConnectAvailable(hasDevice);
                    _trayIcon->RefreshQuickConnectMenu();
                },
                Qt::QueuedConnection);
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

void ApbApplication::OnManualUpdateCheckCompleted(const Core::Update::CheckResult &result)
{
    const QString version =
        result.release.has_value() ? result.release->version.toString() : QString{};
    _trayIcon->SetUpdateCheckFeedback(result.status, version);

    if (result.status == Core::Update::CheckStatus::UpdateAvailable && result.release.has_value()) {
        _trayIcon->VersionUpdateAvailable(result.release.value());
        _mainWindow->AskUserUpdate(result.release.value());
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
    if (_nativeToastNotifications && Core::Notifications::ShowToast(title, text)) {
        return;
    }
    _trayIcon->ShowMessage(title, text);
}

void ApbApplication::ConnectDevice()
{
    _quickConnect->RequestAutomatic();
}

void ApbApplication::SwitchAudioOutputToAirPods()
{
    _audioControl->SwitchOutputToAirPods();
}

void ApbApplication::RestorePreviousAudioOutput()
{
    _audioControl->RestorePreviousOutput();
}

QString ApbApplication::BuildStatusJson() const
{
    const auto snap = _airPodsManager->GetDiagnostics();
    nlohmann::json root;
    root["ok"] = true;
    root["version"] = Config::Version::String;
    root["portable"] = Utils::File::IsPortableMode();
    root["boundDevice"] = snap.boundDeviceName.toStdString();
    root["model"] = Helper::ToString(snap.boundModel).toStdString();
    root["bluetoothConnected"] = snap.bluetoothConnected;
    root["transport"] = snap.aapConnected ? "aap_l2cap" : "ble_broadcast";
    root["transportBackend"] = snap.transportBackend.toStdString();
    root["aapDriverAvailable"] = snap.aapDriverAvailable;
    if (snap.radioServiceInfo.has_value()) {
        const auto &rs = snap.radioServiceInfo.value();
        nlohmann::json rsJson;
        rsJson["installed"] = rs.serviceInstalled;
        rsJson["running"] = rs.serviceRunning;
        rsJson["state"] = rs.stateName;
        rsJson["usbipReady"] = rs.usbipDriverReady;
        rsJson["winUsbBound"] = rs.winUsbBound;
        rsJson["virtualAdapterAttached"] = rs.virtualAdapterAttached;
        rsJson["guardTripped"] = rs.guardTripped;
        rsJson["activeAapChannels"] = rs.activeAapChannels;
        rsJson["activePollLatencyUs"] = rs.activePollLatencyUs;
        rsJson["virtualMicAttached"] = rs.virtualMicAttached;
        rsJson["virtualMicCapturing"] = rs.virtualMicCapturing;
        rsJson["adapterDisplayName"] = rs.adapterDisplayName;
        rsJson["injectedPackets"] = rs.injectedPackets;
        rsJson["interceptedPackets"] = rs.interceptedPackets;
        root["radioService"] = rsJson;
    }
    root["scannerRunning"] = snap.scannerRunning;
    if (snap.lastRssi.has_value()) {
        root["rssi"] = snap.lastRssi.value();
    }
    if (!snap.lastAdvHex.isEmpty()) {
        root["lastAdvRedacted"] = snap.lastAdvHex.toStdString();
    }

    const auto profile = _audioControl->CurrentProfile();
    root["audioProfile"] = profile == Core::AirPods::AudioProfile::HandsFree ? "hands_free"
                           : profile == Core::AirPods::AudioProfile::Stereo  ? "stereo"
                                                                             : "unknown";
    if (const auto proc = _audioControl->CurrentHfpProcess(); !proc.isEmpty()) {
        root["hfpProcess"] = proc.toStdString();
    }

    const auto lat = _audioControl->GetLatencyDiagnostics();
    nlohmann::json latJson;
    latJson["codec"] = lat.codec.toStdString();
    latJson["aacEnabled"] = lat.aacEnabled;
    latJson["enhancementsEnabled"] = lat.enhancementsEnabled;
    latJson["enginePeriodMs"] = lat.enginePeriodMs;
    latJson["minPeriodMs"] = lat.minPeriodMs;
    latJson["keepAliveEnabled"] = lat.keepAliveEnabled;
    latJson["keepAliveActive"] = lat.keepAliveActive;
    latJson["lowLatencyProfile"] = lat.IsLowLatencyProfile();
    root["latency"] = latJson;

    if (snap.currentState.has_value()) {
        const auto &st = snap.currentState.value();
        root["displayName"] = st.displayName.toStdString();
        root["batteryPrecision"] =
            st.batteryPrecision == Core::AirPods::BatteryPrecision::ExactPercent ? "1%" : "10%";
        if (!st.firmwareVersion.isEmpty()) {
            root["firmwareVersion"] = st.firmwareVersion.toStdString();
        }
        if (st.estimatedMinutesRemaining.has_value()) {
            root["estimatedMinutesRemaining"] = st.estimatedMinutesRemaining.value();
        }
        root["asymmetricBatteryWarning"] = st.asymmetricBatteryWarning;

        auto encodePod = [](const Core::AirPods::PodState &p) {
            nlohmann::json j;
            if (p.battery.Available()) {
                j["battery"] = p.battery.Value();
            }
            j["charging"] = p.isCharging;
            j["inEar"] = p.isInEar;
            return j;
        };
        root["left"] = encodePod(st.pods.left);
        root["right"] = encodePod(st.pods.right);

        nlohmann::json box;
        if (st.caseBox.battery.Available()) {
            box["battery"] = st.caseBox.battery.Value();
        }
        box["charging"] = st.caseBox.isCharging;
        box["lidOpened"] = st.caseBox.isLidOpened;
        box["bothInCase"] = st.caseBox.isBothPodsInCase;
        root["case"] = box;

        nlohmann::json nc;
        nc["controllable"] = st.noiseControl.controllable;
        if (st.noiseControl.activeMode.has_value()) {
            nc["mode"] = Helper::ToString(st.noiseControl.activeMode.value()).toStdString();
        }
        if (st.noiseControl.conversationalAwareness.has_value()) {
            nc["conversationalAwareness"] = st.noiseControl.conversationalAwareness.value();
        }
        if (st.noiseControl.heartRateMonitoringEnabled.has_value()) {
            nc["heartRateMonitoringEnabled"] = st.noiseControl.heartRateMonitoringEnabled.value();
        }
        if (st.noiseControl.heartRateBpm.has_value()) {
            nc["heartRateBpm"] = st.noiseControl.heartRateBpm.value();
        }
        if (st.noiseControl.heartRateMinBpm.has_value()) {
            nc["heartRateMinBpm"] = st.noiseControl.heartRateMinBpm.value();
        }
        if (st.noiseControl.heartRateMaxBpm.has_value()) {
            nc["heartRateMaxBpm"] = st.noiseControl.heartRateMaxBpm.value();
        }
        root["noiseControl"] = nc;
    }

    return QString::fromStdString(root.dump(2));
}

QByteArray ApbApplication::HandleIpcRequest(const QByteArray &requestJson)
{
    QByteArray reply;
    QMetaObject::invokeMethod(
        this,
        [this, requestJson, &reply] {
            try {
                const auto req = nlohmann::json::parse(requestJson.toStdString());
                const std::string cmd = req.value("cmd", "");

                if (cmd == "status") {
                    reply = BuildStatusJson().toUtf8();
                    return;
                }
                if (cmd == "connect") {
                    _quickConnect->RequestConnect();
                    reply = QByteArrayLiteral("{\"ok\":true,\"action\":\"connect\"}");
                    return;
                }
                if (cmd == "disconnect") {
                    _quickConnect->RequestDisconnect();
                    reply = QByteArrayLiteral("{\"ok\":true,\"action\":\"disconnect\"}");
                    return;
                }
                if (cmd == "toggle") {
                    _quickConnect->RequestToggle();
                    reply = QByteArrayLiteral("{\"ok\":true,\"action\":\"toggle\"}");
                    return;
                }
                if (cmd == "popup") {
                    _mainWindow->ToggleVisibility();
                    reply = QByteArrayLiteral("{\"ok\":true,\"action\":\"popup\"}");
                    return;
                }
                if (cmd == "settings") {
                    _trayIcon->OpenSettings();
                    reply = QByteArrayLiteral("{\"ok\":true,\"action\":\"settings\"}");
                    return;
                }
                if (cmd == "mode") {
                    const std::string val = req.value("value", "");
                    bool sent = false;
                    if (val == "cycle") {
                        sent = _airPodsManager->CycleNoiseControlMode();
                    }
                    else if (val == "off") {
                        sent = _airPodsManager->SetNoiseControlMode(
                            Core::AirPods::NoiseControlMode::Off);
                    }
                    else if (val == "anc") {
                        sent = _airPodsManager->SetNoiseControlMode(
                            Core::AirPods::NoiseControlMode::NoiseCancellation);
                    }
                    else if (val == "transparency" || val == "aware") {
                        sent = _airPodsManager->SetNoiseControlMode(
                            Core::AirPods::NoiseControlMode::Transparency);
                    }
                    else if (val == "adaptive") {
                        sent = _airPodsManager->SetNoiseControlMode(
                            Core::AirPods::NoiseControlMode::Adaptive);
                    }
                    nlohmann::json res{
                        {"ok", sent},
                        {"action", "mode"},
                        {"value", val},
                    };
                    if (!sent) {
                        res["error"] = "AAP control transport not connected or invalid mode";
                    }
                    reply = QByteArray::fromStdString(res.dump());
                    return;
                }

                reply = QByteArrayLiteral("{\"ok\":false,\"error\":\"unknown command\"}");
            }
            catch (...) {
                reply = QByteArrayLiteral("{\"ok\":false,\"error\":\"malformed json\"}");
            }
        },
        Qt::BlockingQueuedConnection);
    return reply;
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
