// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

//
// Renders the battery popup to PNG files, once per model.
//
// Not a test. The popup shows Apple's artwork for whichever device is in range, and nothing
// can assert that AirPods look like AirPods; this is how every model gets looked at without
// a device, a Bluetooth adapter, or the registry. The window is built against an in-memory
// settings store, fed a plausible state per model, never shown on screen, and grabbed, in
// light and then in dark.
//
//     PopupPreview <output directory>
//

#include <memory>

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QIcon>
#include <QTimer>

#include <Config.h>
#include "Source/Core/Settings.h"
#include "Source/Core/SettingsRepository.h"
#include "Source/Domain/AirPodsState.h"
#include "Source/Gui/FrameClock.h"
#include "Source/Gui/MainWindow.h"
#include "Source/Gui/ThemeController.h"
#include "Source/Presentation/DeviceArtwork.h"
#include "Source/Presentation/Theme.h"
#include "Source/Support/Helper.h"

namespace {

void Settle(int milliseconds)
{
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

QString FileName(const QString &label, const char *suffix)
{
    QString name = label;
    name.replace(' ', '_').remove('(').remove(')');
    return QStringLiteral("Popup-%1%2.png").arg(name, suffix);
}

bool Grab(QWidget &window, const QDir &output, const QString &label, const char *suffix)
{
    Settle(150);
    const auto path = output.filePath(FileName(label, suffix));
    if (!window.grab().save(path)) {
        qWarning("Failed to write %s", qPrintable(path));
        return false;
    }
    qInfo("Wrote %s", qPrintable(path));
    return true;
}

Core::AirPods::State StateFor(Core::AirPods::Model model, uint8_t colour)
{
    Core::AirPods::State state;
    state.model = model;
    state.colour = colour;
    state.displayName = Helper::ToString(model);
    state.pods.left.battery = 80;
    state.pods.left.isCharging = true;
    state.pods.right.battery = 65;
    state.caseBox.battery = 45;
    state.caseBox.isCharging = true;
    state.caseBox.isLidOpened = true;
    return state;
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
    // And the clock its animations run on, so the device loop takes the path it does in
    // the application.
    Gui::FrameClock clock;

    auto repository = std::make_unique<Core::Settings::MemoryRepository>();
    repository->Write("abi_version", Core::Settings::kFieldsAbiVersion);
    Core::Settings::SetRepository(std::move(repository));
    Core::Settings::Load();

    Gui::MainWindow window;
    // Lays the window out and runs showEvent without putting a window on the desktop.
    window.setAttribute(Qt::WA_DontShowOnScreen);
    window.show();
    Settle(200);

    struct Look {
        Core::Settings::Appearance appearance;
        const char *suffix;
    };
    constexpr Look kLooks[] = {
        {Core::Settings::Appearance::Light, ""},
        {Core::Settings::Appearance::Dark, "-dark"},
    };

    for (const auto &look : kLooks) {
        theme.SetAppearance(look.appearance);

        window.Unavailable();
        if (!Grab(window, output, "Unavailable", look.suffix)) {
            return 1;
        }
        window.Unbind();
        if (!Grab(window, output, "Unbound", look.suffix)) {
            return 1;
        }

        window.Available();
        window.Bind();
        window.SetBoundDevice(Core::AirPods::Model::AirPods_Pro_3, QStringLiteral("AirPods Pro 3"));
        window.SetQuickConnectAvailable(true);
        if (!Grab(window, output, "Disconnected_Connect", look.suffix)) {
            return 1;
        }
        window.SetQuickConnectAvailable(false);

        using Core::AirPods::Model;
        for (uint32_t i = 1; i < Helper::ToUnderlying(Model::_Max); ++i) {
            const auto model = static_cast<Model>(i);
            window.UpdateState(StateFor(model, 0));
            if (!Grab(window, output, Helper::ToString(model), look.suffix)) {
                return 1;
            }
        }

        // Apple Control Center Noise Control preview (AirPods Pro 3 with ANC + Conversation + live
        // Heart Rate pill bar).
        auto aapState = StateFor(Model::AirPods_Pro_3, 0);
        aapState.batteryPrecision = Core::AirPods::BatteryPrecision::ExactPercent;
        aapState.noiseControl.controllable = true;
        aapState.noiseControl.activeMode = Core::AirPods::NoiseControlMode::NoiseCancellation;
        aapState.noiseControl.conversationalAwareness = true;
        aapState.noiseControl.heartRateMonitoringEnabled = true;
        aapState.noiseControl.heartRateStatus = Core::AirPods::HeartRateStatus::Live;
        aapState.noiseControl.heartRateBpm = 72;
        aapState.noiseControl.heartRateConfidence = 0xE6;
        aapState.noiseControl.heartRateHistory = {66, 68, 71, 74, 72, 70, 75, 79, 76, 73, 72};
        window.UpdateState(aapState);
        if (!Grab(window, output, "AirPods Pro 3 ANC", look.suffix)) {
            return 1;
        }

        // Apple Adaptive Audio + Hearing Aid preview.
        auto adaptiveState = StateFor(Model::AirPods_Pro_3, 0);
        adaptiveState.batteryPrecision = Core::AirPods::BatteryPrecision::ExactPercent;
        adaptiveState.noiseControl.controllable = true;
        adaptiveState.noiseControl.activeMode = Core::AirPods::NoiseControlMode::Adaptive;
        adaptiveState.noiseControl.adaptiveNoiseLevel = 75;
        adaptiveState.noiseControl.conversationalAwareness = true;
        adaptiveState.noiseControl.conversationalAwarenessSpeaking = true;
        adaptiveState.noiseControl.hearingAidEnabled = true;
        window.UpdateState(adaptiveState);
        if (!Grab(window, output, "AirPods Pro 3 Adaptive", look.suffix)) {
            return 1;
        }

        // Non-Adaptive 3-stop capsule preview (AirPods Pro 1).
        auto pro1Anc = StateFor(Model::AirPods_Pro, 0);
        pro1Anc.batteryPrecision = Core::AirPods::BatteryPrecision::ExactPercent;
        pro1Anc.noiseControl.controllable = true;
        pro1Anc.noiseControl.activeMode = Core::AirPods::NoiseControlMode::Transparency;
        window.UpdateState(pro1Anc);
        if (!Grab(window, output, "AirPods Pro 1 Transparency", look.suffix)) {
            return 1;
        }

        // Nearby other-device popup (AirPods connected to iPhone/iPad, live battery + Connect).
        window.Disconnect();
        window.SetQuickConnectAvailable(true);
        auto nearbyState = StateFor(Model::AirPods_Pro_3, 0);
        nearbyState.isNearbyOtherDevice = true;
        nearbyState.caseBox.isLidOpened = false;
        window.UpdateNearbyState(nearbyState, false);
        if (!Grab(window, output, "AirPods Pro 3 Nearby", look.suffix)) {
            return 1;
        }
        window.SetConnecting(true);
        if (!Grab(window, output, "AirPods Pro 3 Nearby Connecting", look.suffix)) {
            return 1;
        }
        window.SetConnecting(false);
        window.ClearNearby();
        window.SetQuickConnectAvailable(false);

        auto hfpState = StateFor(Model::AirPods_Pro_3, 0);
        hfpState.audioProfile = Core::AirPods::AudioProfile::HandsFree;
        hfpState.audioProfileSource = QStringLiteral("Discord.exe");
        window.UpdateState(hfpState);
        if (!Grab(window, output, "AirPods Pro 3 HandsFree", look.suffix)) {
            return 1;
        }

        // A colour Apple publishes, and one it does not, which falls back to the default.
        window.UpdateState(StateFor(Model::AirPods_Max_USB_C, 20));
        if (!Grab(window, output, "AirPods Max USB-C colour 20", look.suffix)) {
            return 1;
        }
        window.UpdateState(StateFor(Model::AirPods_Max_USB_C, 99));
        if (!Grab(window, output, "AirPods Max USB-C colour 99", look.suffix)) {
            return 1;
        }
    }
    return 0;
}
