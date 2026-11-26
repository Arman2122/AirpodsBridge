// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

//
// Renders every page of the settings window to a PNG file.
//
// Not a test. The settings window has no automated coverage -- nothing can assert that a
// switch looks like a switch -- so this is how it gets looked at without a tray icon, a
// Bluetooth adapter, or the registry: the window is built against an in-memory settings
// store seeded with plausible values, never shown on screen, and grabbed page by page, in
// light and then in dark. The same images serve as README screenshots.
//
//     SettingsPreview <output directory>
//

#include <memory>

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QIcon>
#include <QScrollArea>
#include <QScrollBar>
#include <QTimer>

#include <Config.h>
#include "Source/Core/QuickConnect.h"
#include "Source/Core/Settings.h"
#include "Source/Core/SettingsRepository.h"
#include "Source/Gui/SettingsWindow.h"
#include "Source/Gui/ThemeController.h"
#include "Source/Presentation/Theme.h"

namespace {

// One paired device, so the quick connect page shows its combo box populated.
class PreviewBackend final : public Core::QuickConnect::Backend
{
public:
    std::vector<Core::QuickConnect::Device> ListDevices(std::stop_token) override
    {
        return {{"{preview}", "AirPods Pro 3", true}};
    }

    bool RequestReconnect(const QString &, std::stop_token) override
    {
        return false;
    }
};

void Settle(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app{argc, argv};

    const QDir output{argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath()};
    if (!output.exists()) {
        qWarning("Output directory does not exist: %s", qPrintable(output.path()));
        return 1;
    }

    Gui::Theme::Font::Register();
    app.setFont(Gui::Theme::Font::Body());
    app.setWindowIcon(QIcon{Config::QrcIconSvg});
    // Before the window, as in the application: it sets the style and the palette.
    Gui::ThemeController theme;

    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    repository->Write("device_address", QVariant::fromValue<qulonglong>(0xF433B7263664ULL));
    repository->Write("bound_device_name", "AirPods Pro 3");
    repository->Write("tray_quick_connect_enabled", true);
    repository->Write("tray_quick_connect_device_id", "{preview}");
    repository->Write("tray_icon_battery", "WhenLowBattery");
    // Off, so the dependent row is rendered in its dimmed state as well.
    repository->Write("automatic_ear_detection", false);
    repository->Write("auto_connect_on_lid_open", true);
    repository->Write("low_battery_threshold", 30u);
    repository->Write("full_charge_notification", true);
    repository->Write("preferred_mic_device_id", "{mic-audient}");
    repository->Write("auto_switch_audio_on_connect", true);
    repository->Write("smart_audio_routing", true);
    repository->Write("volume_cap_enabled", true);
    repository->Write("volume_cap_level", 75u);
    repository->Write("audio_keep_alive", true);
    repository->Write("hotkey_toggle_connect", "Ctrl+Alt+A");
    repository->Write("hotkey_toggle_popup", "Ctrl+Alt+B");
    Core::Settings::SetRepository(std::move(repository));
    Core::Settings::Load();

    Core::QuickConnect::Controller quickConnect{std::make_shared<PreviewBackend>()};

    Gui::SettingsWindow window{quickConnect};
    window.SetAudioEndpointsProvider([] {
        return std::vector<Core::AudioControl::AudioEndpoint>{
            {.id = "{mic-audient}", .name = "Analogue 1/2 (Audient iD14)", .isDefault = true},
            {.id = "{mic-razer}", .name = "Microphone (Razer Kraken V4 - Chat)"},
        };
    });
    window.SetDiagnosticsProvider([] {
        return Gui::SettingsWindow::DiagnosticsInfo{
            .transportSummary =
                "AAP L2CAP connected via No-Test-Mode Radio Service (WinUSB + usbip-win2).",
            .connectionSummary =
                "AirPods Pro 3 · Connected · A2DP Stereo (AAC) · Stream awake · -58 dBm",
            .clipboardReport = "{\"ok\":true}",
        };
    });
    window.SetRadioControlProvider([] {
        Core::AirPods::State previewState;
        previewState.model = Core::AirPods::Model::AirPods_Pro_3;
        previewState.displayName = QStringLiteral("AirPods Pro 3");
        previewState.noiseControl.controllable = true;
        previewState.noiseControl.activeMode = Core::AirPods::NoiseControlMode::Adaptive;
        previewState.noiseControl.conversationalAwareness = true;
        previewState.noiseControl.adaptiveNoiseLevel = 65;
        previewState.noiseControl.personalizedVolume = true;
        previewState.noiseControl.oneBudAnc = true;
        previewState.noiseControl.loudSoundReduction = true;
        previewState.noiseControl.allowOffListeningMode = true;
        previewState.noiseControl.microphoneSide = Core::AirPods::MicrophoneSide::Auto;
        previewState.noiseControl.toneVolume = 85;
        previewState.noiseControl.headGestures = true;
        previewState.noiseControl.hearingAidEnabled = false;
        previewState.noiseControl.mediaAssistEnabled = false;
        previewState.noiseControl.heartRateMonitoringEnabled = true;
        previewState.noiseControl.heartRateStatus = Core::AirPods::HeartRateStatus::Live;
        previewState.noiseControl.heartRateBpm = 72;
        previewState.noiseControl.heartRateConfidence = 0xE6;
        previewState.noiseControl.heartRateMinBpm = 64;
        previewState.noiseControl.heartRateMaxBpm = 88;
        previewState.noiseControl.heartRateAvgBpm = 73;
        previewState.noiseControl.heartRateHistory = {64, 67, 70, 74, 71, 69,
                                                      73, 79, 88, 82, 76, 72};
        return Gui::SettingsWindow::RadioControlInfo{
            .engineHeadline = QStringLiteral("Active"),
            .engineDetail = QStringLiteral("Active %1 Connected").arg(QChar{0x00B7}),
            .adapterSummary =
                QStringLiteral("Intel Wireless Bluetooth %1 Active").arg(QChar{0x00B7}),
            .serviceInstalled = true,
            .serviceRunning = true,
            .guardTripped = false,
            .aapConnected = true,
            .heartRateServiceId = uint8_t{20},
            .state = previewState,
        };
    });
    window.SetLatencyDiagnosticsProvider([] {
        return Core::AudioControl::LatencyDiagnostics{
            .codec = QStringLiteral("AAC"),
            .aacEnabled = true,
            .enhancementsEnabled = true,
            .enginePeriodMs = 10.0,
            .minPeriodMs = 3.0,
            .keepAliveEnabled = true,
            .keepAliveActive = true,
        };
    });
    // Lays the window out and runs showEvent without putting a window on the desktop.
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    quickConnect.RefreshDevices();
    Settle(400);

    // The setting drives the look the way it does in the application.
    const auto watch = Core::Settings::Watch(
        &Core::Settings::Fields::appearance,
        [&theme](Core::Settings::Appearance appearance) { theme.SetAppearance(appearance); });

    struct Page {
        Gui::SettingsWindow::Section section;
        const char *name;
    };
    constexpr Page kPages[] = {
        {Gui::SettingsWindow::Section::General, "General"},
        {Gui::SettingsWindow::Section::Display, "Display"},
        {Gui::SettingsWindow::Section::Control, "Control"},
        {Gui::SettingsWindow::Section::Health, "Health"},
        {Gui::SettingsWindow::Section::Audio, "Audio"},
        {Gui::SettingsWindow::Section::Features, "Features"},
        {Gui::SettingsWindow::Section::About, "About"},
    };

    struct Look {
        Core::Settings::Appearance appearance;
        const char *suffix;
    };
    constexpr Look kLooks[] = {
        {Core::Settings::Appearance::Light, ""},
        {Core::Settings::Appearance::Dark, "-dark"},
    };

    for (const auto &look : kLooks) {
        Core::Settings::ModifiableAccess()->appearance = look.appearance;
        // Showing again reloads the controls, so the theme row shows the mode on show.
        window.hide();
        window.show();
        Settle(100);

        for (const auto &page : kPages) {
            window.SelectSection(page.section);
            Settle(100);

            const auto path =
                output.filePath(QStringLiteral("Settings-%1%2.png").arg(page.name, look.suffix));
            if (!window.grab().save(path)) {
                qWarning("Failed to write %s", qPrintable(path));
                return 1;
            }
            qInfo("Wrote %s", qPrintable(path));

            // A page taller than the window is captured a second time, scrolled to the
            // end, so nothing below the fold goes unseen.
            for (auto *scroll : window.findChildren<QScrollArea *>()) {
                auto *bar = scroll->verticalScrollBar();
                if (!scroll->isVisible() || bar->maximum() == 0) {
                    continue;
                }
                if (bar->maximum() > 450) {
                    bar->setValue(bar->maximum() * 32 / 100);
                    Settle(50);
                    const auto midPath = output.filePath(
                        QStringLiteral("Settings-%1-mid%2.png").arg(page.name, look.suffix));
                    if (window.grab().save(midPath)) {
                        qInfo("Wrote %s", qPrintable(midPath));
                    }

                    bar->setValue(bar->maximum() * 68 / 100);
                    Settle(50);
                    const auto lowerPath = output.filePath(
                        QStringLiteral("Settings-%1-lower%2.png").arg(page.name, look.suffix));
                    if (window.grab().save(lowerPath)) {
                        qInfo("Wrote %s", qPrintable(lowerPath));
                    }
                }
                bar->setValue(bar->maximum());
                Settle(50);
                const auto scrolledPath = output.filePath(
                    QStringLiteral("Settings-%1-scrolled%2.png").arg(page.name, look.suffix));
                if (window.grab().save(scrolledPath)) {
                    qInfo("Wrote %s", qPrintable(scrolledPath));
                }
                bar->setValue(0);
            }
        }
    }
    return 0;
}
