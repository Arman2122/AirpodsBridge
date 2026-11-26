// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/SettingsWindow.h"

#include <array>
#include <cstdlib>

#include <QClipboard>
#include <QDesktopServices>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <Config.h>

#include "Source/Core/Debug.h"
#include "Source/Gui/Widget/AppleNoiseControl.h"
#include "Source/Gui/Widget/ComboBox.h"
#include "Source/Gui/Widget/HeartRateCard.h"
#include "Source/Gui/Widget/Label.h"
#include "Source/Gui/Widget/ProgressBar.h"
#include "Source/Gui/Widget/PushButton.h"
#include "Source/Gui/Widget/ScrollArea.h"
#include "Source/Gui/Widget/SegmentedControl.h"
#include "Source/Gui/Widget/SettingsCard.h"
#include "Source/Gui/Widget/Sidebar.h"
#include "Source/Gui/Widget/Slider.h"
#include "Source/Gui/Widget/Toggle.h"
#include "Source/Presentation/Theme.h"
#include "Source/Support/Logger.h"
#include "Source/Support/Utils.h"

namespace Gui {

using Core::Settings::Fields;
using Core::Settings::GetConstMetaFields;
using Core::Settings::GetCurrent;
using Core::Settings::GetDefault;
using Core::Settings::ModifiableAccess;
using Core::Settings::Save;
using Core::Settings::TaskbarStatusBehavior;
using Core::Settings::TrayIconBatteryBehavior;

namespace {

constexpr QSize kWindowSize{720, 600};

// The choices offered for "low battery". The stored value is a percentage, so a value
// set by hand still lands on the nearest choice.
constexpr std::array<uint32_t, 3> kLowBatteryLevels{10, 20, 30};

// The slider holds the range as a positive number, so it reads left-to-right as
// "nearer" to "farther". -50 dBm is only a device on the desk; -100 dBm is as far as the
// radio can hear at all.
constexpr int kRangeSliderMin = 50;
constexpr int kRangeSliderMax = 100;
constexpr int kVolumeCapMin = 40;
constexpr int kVolumeCapMax = 100;

template <class T>
void WriteSetting(T Fields::*field, T value)
{
    // The accessor saves and applies the change when it goes out of scope. It exposes the
    // fields through `operator->`, which `->*` does not go through, hence the spelling.
    auto settings = ModifiableAccess();
    Fields *fields = settings.operator->();
    fields->*field = std::move(value);
}

using Refresher = std::function<void(const Fields &)>;

void BindHotkeyCombo(
    Widget::ComboBox *combo, QString Fields::*field, const QStringList &presets,
    std::vector<Refresher> &refreshers)
{
    combo->setMinimumWidth(180);
    combo->addItem(QObject::tr("Off"), QString{});
    for (const auto &preset : presets) {
        combo->addItem(preset, preset);
    }

    QObject::connect(
        combo, qOverload<int>(&QComboBox::currentIndexChanged), combo, [combo, field](int idx) {
            if (idx < 0) {
                return;
            }
            WriteSetting(field, combo->itemData(idx).toString());
        });

    refreshers.push_back([combo, field](const Fields &fields) {
        const QSignalBlocker blocker{combo};
        const QString val = (fields.*field).trimmed();
        int idx = combo->findData(val);
        if (idx < 0 && !val.isEmpty()) {
            combo->addItem(val, val);
            idx = combo->count() - 1;
        }
        combo->setCurrentIndex(idx >= 0 ? idx : 0);
    });
}

void BindToggle(Widget::Toggle *toggle, bool Fields::*field, std::vector<Refresher> &refreshers)
{
    QObject::connect(toggle, &QAbstractButton::toggled, toggle, [field](bool checked) {
        WriteSetting(field, checked);
    });
    refreshers.push_back([toggle, field](const Fields &fields) {
        const QSignalBlocker blocker{toggle};
        toggle->setChecked(fields.*field);
    });
}

// The enumerations behind a segmented control count up from zero in the order the
// segments are added, so the index is the value.
template <class EnumT>
void BindSegments(
    Widget::SegmentedControl *control, EnumT Fields::*field, std::vector<Refresher> &refreshers)
{
    QObject::connect(
        control, &Widget::SegmentedControl::CurrentIndexChanged, control,
        [field](int index) { WriteSetting(field, static_cast<EnumT>(index)); });
    refreshers.push_back([control, field](const Fields &fields) {
        const QSignalBlocker blocker{control};
        control->SetCurrentIndex(static_cast<int>(fields.*field));
    });
}

// A segmented control over a list of values rather than an enumeration.
template <class T, size_t N>
void BindValues(
    Widget::SegmentedControl *control, T Fields::*field, const std::array<T, N> &values,
    std::vector<Refresher> &refreshers)
{
    QObject::connect(
        control, &Widget::SegmentedControl::CurrentIndexChanged, control,
        [field, values](int index) {
            if (index >= 0 && index < static_cast<int>(values.size())) {
                WriteSetting(field, values.at(static_cast<size_t>(index)));
            }
        });
    refreshers.push_back([control, field, values](const Fields &fields) {
        const auto distance = [&](size_t i) {
            return std::abs(
                static_cast<long long>(values.at(i)) - static_cast<long long>(fields.*field));
        };
        size_t nearest = 0;
        for (size_t i = 1; i < values.size(); ++i) {
            if (distance(i) < distance(nearest)) {
                nearest = i;
            }
        }
        const QSignalBlocker blocker{control};
        control->SetCurrentIndex(static_cast<int>(nearest));
    });
}

Widget::PushButton *MakeButton(const QString &text, QWidget *parent)
{
    auto *button = new Widget::PushButton{parent};
    button->setText(text);
    button->setFixedHeight(30);
    button->setMinimumWidth(112);
    return button;
}

Widget::Label *
MakeLabel(const QString &text, const QFont &font, Widget::Label::Role role, QWidget *parent)
{
    auto *label = new Widget::Label{text, role, parent};
    label->setFont(font);
    return label;
}

//
// The About page's rich text. A colour written into HTML does not follow the palette, so
// these are functions rather than strings: the window calls them again when the mode
// changes.
//

QString LinkHtml(const QString &url, const QString &text, const QColor &colour)
{
    return QStringLiteral("<a href=\"%1\" style=\"color:%3;text-decoration:none\">%2</a>")
        .arg(url, text, colour.name());
}

QString VersionHtml()
{
    const auto &accent = Theme::Colours().Accent;
    QString html =
        LinkHtml(Config::UrlCurrentRelease, QStringLiteral("v") + CONFIG_VERSION_STRING, accent);
#if defined APB_BUILD_GIT_HASH
    html += QStringLiteral(" &nbsp;") +
            LinkHtml(
                QStringLiteral("%1/commit/%2").arg(Config::UrlRepository, APB_BUILD_GIT_HASH),
                QString{APB_BUILD_GIT_HASH}.left(7), accent);
#endif
    return html;
}

struct Library {
    const char *name, *url, *licence, *licenceUrl;
};

constexpr Library kLibraries[] = {
    // clang-format off
    { "Qt 5", "https://www.qt.io/download-qt-installer", "LGPLv3", "https://doc.qt.io/qt-5/lgpl.html" },
    { "spdlog", "https://github.com/gabime/spdlog", "MIT", "https://github.com/gabime/spdlog/blob/v1.x/LICENSE" },
    { "cxxopts", "https://github.com/jarro2783/cxxopts", "MIT", "https://github.com/jarro2783/cxxopts/blob/master/LICENSE" },
    { "cpr", "https://github.com/whoshuu/cpr", "MIT", "https://github.com/whoshuu/cpr/blob/master/LICENSE" },
    { "json", "https://github.com/nlohmann/json", "MIT", "https://github.com/nlohmann/json/blob/develop/LICENSE.MIT" },
    { "SingleApplication", "https://github.com/itay-grudev/SingleApplication", "MIT", "https://github.com/itay-grudev/SingleApplication/blob/master/LICENSE" },
    { "pfr", "https://github.com/boostorg/pfr", "BSL-1.0", "https://github.com/boostorg/pfr/blob/develop/LICENSE_1_0.txt" },
    { "magic_enum", "https://github.com/Neargye/magic_enum", "MIT", "https://github.com/Neargye/magic_enum/blob/master/LICENSE" },
    { "stacktrace", "https://github.com/boostorg/stacktrace", "BSL-1.0", "https://www.boost.org/LICENSE_1_0.txt" },
    { "Inter", "https://rsms.me/inter/", "SIL OFL 1.1", "https://github.com/rsms/inter/blob/master/LICENSE.txt" },
    { "usbip-win2", "https://github.com/vadimgrn/usbip-win2", "BSD-2-Clause", "https://github.com/vadimgrn/usbip-win2/blob/master/LICENSE" },
    // clang-format on
};

QString CreditsHtml()
{
    const auto &colours = Theme::Colours();
    QString html = QStringLiteral("<table cellspacing=\"0\" cellpadding=\"1\">");
    for (const auto &library : kLibraries) {
        html += QStringLiteral("<tr><td>%1</td><td width=\"18\"></td><td>%2</td></tr>")
                    .arg(
                        LinkHtml(library.url, library.name, colours.Accent),
                        LinkHtml(library.licenceUrl, library.licence, colours.TextSecondary));
    }
    html += QStringLiteral("</table>");
    return html;
}

QWidget *MakeColumn(QWidget *parent)
{
    auto *column = new QWidget{parent};
    auto *layout = new QVBoxLayout{column};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(20);
    return column;
}

QString RangeText(int sliderValue)
{
    return QStringLiteral("%1 dBm").arg(-sliderValue);
}

} // namespace

//////////////////////////////////////////////////

SettingsWindow::SettingsWindow(Core::QuickConnect::Controller &quickConnect, QWidget *parent)
    : QDialog{parent}, _quickConnect{quickConnect}
{
    _pageIndex.fill(-1);

    setWindowTitle(tr("AirpodsBridge Settings"));
    setFixedSize(kWindowSize);

    _sidebar = new Widget::Sidebar{this};
    _pages = new QStackedWidget{this};

    const auto addPage = [this](Section section, const QString &title, QWidget *content) {
        const int index = _pages->addWidget(WrapPage(title, content));
        _sidebar->AddItem(title);
        _pageIndex.at(static_cast<size_t>(section)) = index;
    };
    addPage(Section::General, tr("General"), BuildGeneralPage());
    addPage(Section::Display, tr("Popup & tray"), BuildDisplayPage());
    addPage(Section::Control, tr("AirPods control"), BuildControlPage());
    addPage(Section::Health, tr("Health"), BuildHealthPage());
    addPage(Section::Audio, tr("Audio & shortcuts"), BuildAudioPage());
    addPage(Section::Features, tr("Features"), BuildFeaturesPage());
    addPage(Section::About, tr("About"), BuildAboutPage());
#if defined APB_DEBUG
    addPage(Section::Debug, QStringLiteral("Debug"), BuildDebugPage());
#endif

    connect(
        _sidebar, &Widget::Sidebar::CurrentIndexChanged, _pages, &QStackedWidget::setCurrentIndex);

    auto *restore = MakeButton(tr("Restore defaults"), this);
    connect(restore, &QPushButton::clicked, this, &SettingsWindow::RestoreDefaults);

    auto *footer = new QHBoxLayout;
    footer->setContentsMargins(
        Theme::Metric::PagePadding, 12, Theme::Metric::PagePadding, Theme::Metric::PagePadding - 8);
    footer->addWidget(restore);
    footer->addStretch();

    auto *column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    column->addWidget(_pages, 1);
    column->addLayout(footer);

    auto *root = new QHBoxLayout{this};
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(_sidebar);
    root->addLayout(column, 1);

    connect(&_quickConnect, &Core::QuickConnect::Controller::DevicesChanged, this, [this] {
        RefreshQuickConnectDevices(GetCurrent());
    });

    Refresh();
}

SettingsWindow::Section SettingsWindow::CurrentSection() const
{
    const int current = _pages->currentIndex();
    for (size_t i = 0; i < _pageIndex.size(); ++i) {
        if (_pageIndex.at(i) == current) {
            return static_cast<Section>(i);
        }
    }
    return Section::General;
}

void SettingsWindow::SelectSection(Section section)
{
    const int index = _pageIndex.at(static_cast<size_t>(section));
    if (index < 0) {
        return;
    }
    _sidebar->SetCurrentIndex(index);
    _pages->setCurrentIndex(index);
}

//////////////////////////////////////////////////

QWidget *SettingsWindow::WrapPage(const QString &title, QWidget *content)
{
    auto *page = new QWidget{this};
    auto *layout = new QVBoxLayout{page};
    layout->setContentsMargins(
        Theme::Metric::PagePadding, Theme::Metric::PagePadding - 4, Theme::Metric::PagePadding,
        Theme::Metric::PagePadding);
    layout->setSpacing(20);
    layout->addWidget(MakeLabel(title, Theme::Font::Heading(), Widget::Label::Role::Primary, page));
    layout->addWidget(content);
    layout->addStretch();

    // A page taller than the window scrolls, and glides on the wheel; the surface shows
    // through everything.
    auto *scroll = new Widget::ScrollArea{this};
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    return scroll;
}

QWidget *SettingsWindow::BuildGeneralPage()
{
    const auto &meta = GetConstMetaFields();
    auto *column = MakeColumn(this);

    auto *startup = new Widget::SettingsGroup{tr("Startup"), column};

    auto *autoRun = new Widget::Toggle;
    startup->AddRow(tr("Launch when Windows starts"), meta.auto_run.Description(), autoRun);
    BindToggle(autoRun, &Fields::auto_run, _refreshers);

    auto *checkForUpdates = new Widget::Toggle;
    startup->AddRow(
        tr("Check for updates automatically"), meta.check_for_updates.Description(),
        checkForUpdates);
    BindToggle(checkForUpdates, &Fields::check_for_updates, _refreshers);

    auto *nativeToasts = new Widget::Toggle;
    startup->AddRow(
        tr("Windows toast notifications"), meta.native_toast_notifications.Description(),
        nativeToasts);
    BindToggle(nativeToasts, &Fields::native_toast_notifications, _refreshers);

    auto *appearance = new Widget::SettingsGroup{tr("Appearance"), column};

    auto *theme = new Widget::SegmentedControl;
    theme->AddSegment(tr("System"));
    theme->AddSegment(tr("Light"));
    theme->AddSegment(tr("Dark"));
    appearance->AddRow(tr("Theme"), meta.appearance.Description(), theme);
    BindSegments(theme, &Fields::appearance, _refreshers);

    auto *device = new Widget::SettingsGroup{tr("AirPods"), column};

    auto *bindControls = new QWidget{column};
    auto *bindLayout = new QHBoxLayout{bindControls};
    bindLayout->setContentsMargins(0, 0, 0, 0);
    bindLayout->setSpacing(8);

    _bindButton = MakeButton(tr("Bind"), bindControls);
    _bindButton->setMinimumWidth(84);
    _unbind = MakeButton(tr("Unbind"), bindControls);
    _unbind->setMinimumWidth(84);
    bindLayout->addWidget(_bindButton);
    bindLayout->addWidget(_unbind);

    _bindingRow = device->AddRow(tr("Bound device"), {}, bindControls);
    connect(_bindButton, &QPushButton::clicked, this, [this] { emit BindDeviceRequested(); });
    connect(_unbind, &QPushButton::clicked, this, [this] {
        auto settings = ModifiableAccess();
        settings->device_address = 0;
        settings->bound_device_name.clear();
        RefreshBinding(GetCurrent());
    });

    // The slider and its readout stack under the description, on their own row, because
    // a slider squeezed to the right of the text is too short to be precise with.
    _range = new Widget::Slider;
    _range->setRange(kRangeSliderMin, kRangeSliderMax);
    // Written on release rather than on every pixel of a drag: each write is a registry
    // save and a settings apply that takes the scanner's lock.
    _range->setTracking(false);
    _rangeValue = MakeLabel({}, Theme::Font::Caption(), Widget::Label::Role::Secondary, column);
    _rangeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _rangeValue->setFixedWidth(64);

    auto *rangeControl = new QWidget{column};
    auto *rangeLayout = new QHBoxLayout{rangeControl};
    rangeLayout->setContentsMargins(0, 0, 0, 0);
    rangeLayout->setSpacing(12);
    rangeLayout->addWidget(_range, 1);
    rangeLayout->addWidget(_rangeValue);
    rangeControl->setFixedWidth(260);

    device->AddRow(tr("Bluetooth range"), meta.rssi_min.Description(), rangeControl);
    connect(_range, &QSlider::sliderMoved, this, [this](int value) {
        _rangeValue->setText(RangeText(value));
    });
    connect(_range, &QSlider::valueChanged, this, [this](int value) {
        _rangeValue->setText(RangeText(value));
        WriteSetting(&Fields::rssi_min, static_cast<int16_t>(-value));
    });

    column->layout()->addWidget(startup);
    column->layout()->addWidget(appearance);
    column->layout()->addWidget(device);
    return column;
}

QWidget *SettingsWindow::BuildDisplayPage()
{
    const auto &meta = GetConstMetaFields();
    auto *column = MakeColumn(this);

    auto *popup = new Widget::SettingsGroup{tr("Popup"), column};

    auto *showOnLidOpen = new Widget::Toggle;
    popup->AddRow(
        tr("Show the popup when the case opens"), meta.show_popup_on_lid_open.Description(),
        showOnLidOpen);
    BindToggle(showOnLidOpen, &Fields::show_popup_on_lid_open, _refreshers);

    auto *showNearby = new Widget::Toggle;
    popup->AddRow(
        tr("Show nearby popup when disconnected"),
        meta.show_nearby_popup_when_disconnected.Description(), showNearby);
    BindToggle(showNearby, &Fields::show_nearby_popup_when_disconnected, _refreshers);

    auto *battery = new Widget::SettingsGroup{tr("Battery level"), column};

    auto *trayBattery = new Widget::SegmentedControl;
    trayBattery->AddSegment(tr("Off"));
    trayBattery->AddSegment(tr("When low"));
    trayBattery->AddSegment(tr("Always"));
    battery->AddRow(tr("On the tray icon"), meta.tray_icon_battery.Description(), trayBattery);
    BindSegments(trayBattery, &Fields::tray_icon_battery, _refreshers);

    auto *taskbarBattery = new Widget::SegmentedControl;
    taskbarBattery->AddSegment(tr("Off"));
    taskbarBattery->AddSegment(tr("Text"));
    taskbarBattery->AddSegment(tr("Icon"));
    battery->AddRow(tr("On the taskbar"), meta.battery_on_taskbar.Description(), taskbarBattery);
    BindSegments(taskbarBattery, &Fields::battery_on_taskbar, _refreshers);

    column->layout()->addWidget(popup);
    column->layout()->addWidget(battery);
    return column;
}

QWidget *SettingsWindow::BuildControlPage()
{
    const auto &meta = GetConstMetaFields();
    auto *column = MakeColumn(this);

    auto *engine = new Widget::SettingsGroup{tr("Control engine"), column};

    _radioPrimaryButton = MakeButton(tr("Enable"), column);
    _radioPrimaryButton->SetPrimary(true);
    _radioStatusRow = engine->AddRow(
        tr("Control engine"), tr("Enables noise control, 1% battery, and heart rate."),
        _radioPrimaryButton);
    connect(_radioPrimaryButton, &QPushButton::clicked, this, [this] {
        SetRadioSetupBusy(true, tr("Approve the administrator prompt to continue..."));
        emit RadioServiceActionRequested(_radioPrimaryAction);
    });

    _radioProgressContainer = new QWidget{column};
    auto *progressLayout = new QVBoxLayout{_radioProgressContainer};
    progressLayout->setContentsMargins(
        Theme::Metric::RowPaddingH, 8, Theme::Metric::RowPaddingH, 10);
    progressLayout->setSpacing(0);
    _radioProgressBar = new Widget::ProgressBar{_radioProgressContainer};
    progressLayout->addWidget(_radioProgressBar);
    _radioProgressContainer->hide();
    engine->AddWidget(_radioProgressContainer);

    auto *autoEnableRadio = new Widget::Toggle;
    engine->AddRow(
        tr("Start automatically"), meta.auto_enable_radio_service.Description(), autoEnableRadio);
    BindToggle(autoEnableRadio, &Fields::auto_enable_radio_service, _refreshers);

    _radioRestoreButton = MakeButton(tr("Restore native"), column);
    _radioRestoreButton->setEnabled(false);
    _radioAdapterRow =
        engine->AddRow(tr("Bluetooth adapter"), tr("Standard Windows driver"), _radioRestoreButton);
    connect(_radioRestoreButton, &QPushButton::clicked, this, [this] {
        SetRadioSetupBusy(true, tr("Restoring native Bluetooth driver..."));
        emit RadioServiceActionRequested(RadioServiceAction::RestoreNative);
    });

    auto *lowLatencyQos = new Widget::Toggle;
    engine->AddRow(
        tr("Low-latency polling"), meta.radio_service_low_latency_qos.Description(), lowLatencyQos);
    BindToggle(lowLatencyQos, &Fields::radio_service_low_latency_qos, _refreshers);

    auto *hfpDecline = new Widget::Toggle;
    engine->AddRow(
        tr("Hands-Free protection"), meta.radio_service_hfp_decline.Description(), hfpDecline);
    BindToggle(hfpDecline, &Fields::radio_service_hfp_decline, _refreshers);

    auto *virtualMic = new Widget::Toggle;
    engine->AddRow(
        tr("Virtual stereo microphone"), meta.radio_service_virtual_mic.Description(), virtualMic);
    BindToggle(virtualMic, &Fields::radio_service_virtual_mic, _refreshers);

    auto *listening = new Widget::SettingsGroup{tr("Listening & noise control"), column};

    _aapNoiseModeContainer = new QWidget{column};
    auto *noiseLayout = new QVBoxLayout{_aapNoiseModeContainer};
    noiseLayout->setContentsMargins(Theme::Metric::RowPaddingH, 12, Theme::Metric::RowPaddingH, 10);
    noiseLayout->setSpacing(8);

    auto *noiseTitle = MakeLabel(
        tr("Listening mode"), Theme::Font::Body(), Widget::Label::Role::Primary,
        _aapNoiseModeContainer);
    auto *noiseSubtitle = MakeLabel(
        tr("Drag or tap to switch listening modes or fine-tune Adaptive balance."),
        Theme::Font::Footnote(), Widget::Label::Role::Secondary, _aapNoiseModeContainer);
    noiseSubtitle->setWordWrap(true);

    _aapNoiseControl = new Widget::AppleNoiseControl{
        Widget::AppleNoiseControl::LayoutMode::CapsuleOnly, _aapNoiseModeContainer};
    connect(
        _aapNoiseControl, &Widget::AppleNoiseControl::NoiseControlModeRequested, this,
        &SettingsWindow::NoiseControlModeRequested);
    connect(
        _aapNoiseControl, &Widget::AppleNoiseControl::AdaptiveNoiseLevelRequested, this,
        &SettingsWindow::AdaptiveNoiseLevelRequested);

    noiseLayout->addWidget(noiseTitle);
    noiseLayout->addWidget(noiseSubtitle);
    noiseLayout->addWidget(_aapNoiseControl);
    listening->AddWidget(_aapNoiseModeContainer);

    _aapConvAwareness = new Widget::Toggle;
    _aapConvAwarenessRow = listening->AddRow(
        tr("Conversation awareness"),
        tr("Lowers media volume and enhances nearby voices when you start speaking."),
        _aapConvAwareness);
    connect(_aapConvAwareness, &QAbstractButton::toggled, this, [this](bool checked) {
        emit ConversationalAwarenessRequested(checked);
    });

    _aapOneBudAnc = new Widget::Toggle;
    _aapOneBudAncRow = listening->AddRow(
        tr("Noise cancellation with one AirPod"),
        tr("Allows Active Noise Cancellation when only a single AirPod is worn."), _aapOneBudAnc);
    connect(_aapOneBudAnc, &QAbstractButton::toggled, this, [this](bool checked) {
        emit OneBudAncRequested(checked);
    });

    _aapPersonalizedVol = new Widget::Toggle;
    _aapPersonalizedVolRow = listening->AddRow(
        tr("Personalized volume"),
        tr("Adjusts media playback volume in response to your surroundings."), _aapPersonalizedVol);
    connect(_aapPersonalizedVol, &QAbstractButton::toggled, this, [this](bool checked) {
        emit PersonalizedVolumeRequested(checked);
    });

    _aapLoudSound = new Widget::Toggle;
    _aapLoudSoundRow = listening->AddRow(
        tr("Loud sound reduction"),
        tr("Attenuates sudden loud environmental noises while in Transparency or Adaptive mode."),
        _aapLoudSound);
    connect(_aapLoudSound, &QAbstractButton::toggled, this, [this](bool checked) {
        emit LoudSoundReductionRequested(checked);
    });

    _aapAllowOff = new Widget::Toggle;
    _aapAllowOffRow = listening->AddRow(
        tr("Allow \"Off\" listening mode"),
        tr("Includes Off in the stem press-and-hold listening mode cycle (AirPods Pro 2 & 3)."),
        _aapAllowOff);
    connect(_aapAllowOff, &QAbstractButton::toggled, this, [this](bool checked) {
        emit AllowOffListeningModeRequested(checked);
    });

    auto *device = new Widget::SettingsGroup{tr("Device, microphone & hearing"), column};

    _aapMicSide = new Widget::SegmentedControl;
    _aapMicSide->AddSegment(tr("Auto"));
    _aapMicSide->AddSegment(tr("Left"));
    _aapMicSide->AddSegment(tr("Right"));
    _aapMicSideRow = device->AddRow(
        tr("Microphone side"),
        tr("Choose whether AirPods automatically switch microphones or stay on one side."),
        _aapMicSide);
    connect(_aapMicSide, &Widget::SegmentedControl::CurrentIndexChanged, this, [this](int idx) {
        if (idx >= 0 && idx <= 2) {
            emit MicrophoneSideRequested(static_cast<Core::AirPods::MicrophoneSide>(idx));
        }
    });

    _aapPressSpeed = new Widget::SegmentedControl;
    _aapPressSpeed->AddSegment(tr("Default"));
    _aapPressSpeed->AddSegment(tr("Slower"));
    _aapPressSpeed->AddSegment(tr("Slowest"));
    _aapPressSpeedRow = device->AddRow(
        tr("Press speed"),
        tr("Adjusts how quickly you must press the AirPods stem for double or triple press."),
        _aapPressSpeed);
    connect(_aapPressSpeed, &Widget::SegmentedControl::CurrentIndexChanged, this, [this](int idx) {
        if (idx >= 0 && idx <= 2) {
            emit PressSpeedRequested(static_cast<Core::AirPods::PressSpeed>(idx));
        }
    });

    _aapPressHold = new Widget::SegmentedControl;
    _aapPressHold->AddSegment(tr("Default"));
    _aapPressHold->AddSegment(tr("Shorter"));
    _aapPressHold->AddSegment(tr("Shortest"));
    _aapPressHoldRow = device->AddRow(
        tr("Press & hold duration"),
        tr("Adjusts how long you press and hold the stem to switch listening modes."),
        _aapPressHold);
    connect(_aapPressHold, &Widget::SegmentedControl::CurrentIndexChanged, this, [this](int idx) {
        if (idx >= 0 && idx <= 2) {
            emit PressHoldDurationRequested(static_cast<Core::AirPods::PressHoldDuration>(idx));
        }
    });

    auto *toneControl = new QWidget{column};
    auto *toneLayout = new QHBoxLayout{toneControl};
    toneLayout->setContentsMargins(0, 0, 0, 0);
    toneLayout->setSpacing(10);
    _aapToneVolumeSlider = new Widget::Slider{toneControl};
    _aapToneVolumeSlider->setRange(15, 100);
    _aapToneVolumeSlider->setFixedWidth(150);
    _aapToneVolumeValue = MakeLabel(
        QStringLiteral("100%"), Theme::Font::Caption(), Widget::Label::Role::Secondary,
        toneControl);
    _aapToneVolumeValue->setFixedWidth(36);
    _aapToneVolumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    toneLayout->addWidget(_aapToneVolumeSlider);
    toneLayout->addWidget(_aapToneVolumeValue);
    _aapToneVolumeRow = device->AddRow(
        tr("Tone volume"), tr("Adjusts volume of AirPods chimes and battery alerts."), toneControl);
    connect(_aapToneVolumeSlider, &QSlider::valueChanged, this, [this](int val) {
        _aapToneVolumeValue->setText(QStringLiteral("%1%").arg(val));
        emit ToneVolumeRequested(static_cast<uint8_t>(val));
    });

    _aapHeadGestures = new Widget::Toggle;
    _aapHeadGesturesRow = device->AddRow(
        tr("Head gestures"), tr("Nod or shake your head to respond to calls and announcements."),
        _aapHeadGestures);
    connect(_aapHeadGestures, &QAbstractButton::toggled, this, [this](bool checked) {
        emit HeadGesturesRequested(checked);
    });

    _aapHearingAid = new Widget::Toggle;
    _aapHearingAidRow = device->AddRow(
        tr("Hearing Aid"),
        tr("Enables Hearing Aid assistance configured on your iPhone or iPad (AirPods Pro 2 & 3)."),
        _aapHearingAid);
    connect(_aapHearingAid, &QAbstractButton::toggled, this, [this](bool checked) {
        emit HearingAidRequested(checked);
    });

    _aapMediaAssist = new Widget::Toggle;
    _aapMediaAssistRow = device->AddRow(
        tr("Media Assist"),
        tr("Applies your personalized audiogram profile to music, video, and calls."),
        _aapMediaAssist);
    connect(_aapMediaAssist, &QAbstractButton::toggled, this, [this](bool checked) {
        emit MediaAssistRequested(checked);
    });

    column->layout()->addWidget(engine);
    column->layout()->addWidget(listening);
    column->layout()->addWidget(device);
    return column;
}

QWidget *SettingsWindow::BuildHealthPage()
{
    const auto &meta = GetConstMetaFields();
    auto *column = MakeColumn(this);

    auto *liveGroup = new Widget::SettingsGroup{tr("Heart Rate"), column};
    _aapHeartRateCard = new Widget::HeartRateCard{column};
    liveGroup->AddWidget(_aapHeartRateCard);

    auto *sensorGroup = new Widget::SettingsGroup{tr("Optical heart rate sensor"), column};

    _aapHeartRate = new Widget::Toggle;
    _aapHeartRateRow = sensorGroup->AddRow(
        tr("Heart rate sensor"), tr("Monitors live optical heart rate while worn."), _aapHeartRate);
    connect(_aapHeartRate, &QAbstractButton::toggled, this, [this](bool checked) {
        emit HeartRateMonitoringRequested(checked);
    });

    auto *showInPopup = new Widget::Toggle;
    sensorGroup->AddRow(
        tr("Show Heart Rate bar in popup"), meta.show_heart_rate_in_popup.Description(),
        showInPopup);
    BindToggle(showInPopup, &Fields::show_heart_rate_in_popup, _refreshers);

    _aapHeartRateTelemetryRow = sensorGroup->AddRow(
        tr("Sensor status"), tr("Connect AirPods Pro 3 or Powerbeats Pro 2 to measure."), nullptr);

    column->layout()->addWidget(liveGroup);
    column->layout()->addWidget(sensorGroup);
    return column;
}

QWidget *SettingsWindow::BuildAudioPage()
{
    const auto &meta = GetConstMetaFields();
    auto *column = MakeColumn(this);

    auto *audio = new Widget::SettingsGroup{tr("Audio protection"), column};

    _preferredMic = new Widget::ComboBox;
    _preferredMic->setMinimumWidth(220);
    audio->AddRow(
        tr("Preferred microphone"), meta.preferred_mic_device_id.Description(), _preferredMic);
    connect(
        _preferredMic, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
            if (index < 0) {
                return;
            }
            WriteSetting(
                &Fields::preferred_mic_device_id, _preferredMic->itemData(index).toString());
        });

    auto *autoSwitchAudio = new Widget::Toggle;
    audio->AddRow(
        tr("Switch audio on connect"), meta.auto_switch_audio_on_connect.Description(),
        autoSwitchAudio);
    BindToggle(autoSwitchAudio, &Fields::auto_switch_audio_on_connect, _refreshers);

    auto *smartRoute = new Widget::Toggle;
    audio->AddRow(
        tr("Switch output when worn"), meta.smart_audio_routing.Description(), smartRoute);
    BindToggle(smartRoute, &Fields::smart_audio_routing, _refreshers);

    _volumeCapToggle = new Widget::Toggle;
    audio->AddRow(
        tr("Limit reconnect volume"), meta.volume_cap_enabled.Description(), _volumeCapToggle);
    BindToggle(_volumeCapToggle, &Fields::volume_cap_enabled, _refreshers);

    _volumeCapSlider = new Widget::Slider;
    _volumeCapSlider->setRange(kVolumeCapMin, kVolumeCapMax);
    _volumeCapSlider->setTracking(false);
    _volumeCapValue = MakeLabel({}, Theme::Font::Caption(), Widget::Label::Role::Secondary, column);
    _volumeCapValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    _volumeCapValue->setFixedWidth(48);

    auto *capControl = new QWidget{column};
    auto *capLayout = new QHBoxLayout{capControl};
    capLayout->setContentsMargins(0, 0, 0, 0);
    capLayout->setSpacing(12);
    capLayout->addWidget(_volumeCapSlider, 1);
    capLayout->addWidget(_volumeCapValue);
    capControl->setFixedWidth(240);

    _volumeCapSliderRow =
        audio->AddRow(tr("Maximum volume"), meta.volume_cap_level.Description(), capControl);
    connect(_volumeCapSlider, &QSlider::sliderMoved, this, [this](int value) {
        _volumeCapValue->setText(QStringLiteral("%1%").arg(value));
    });
    connect(_volumeCapSlider, &QSlider::valueChanged, this, [this](int value) {
        _volumeCapValue->setText(QStringLiteral("%1%").arg(value));
        WriteSetting(&Fields::volume_cap_level, static_cast<uint32_t>(value));
    });
    connect(_volumeCapToggle, &QAbstractButton::toggled, this, [this](bool enabled) {
        _volumeCapSliderRow->setEnabled(enabled);
    });

    auto *latency = new Widget::SettingsGroup{tr("Low-latency & gaming"), column};

    auto *keepAlive = new Widget::Toggle;
    latency->AddRow(tr("Keep audio stream awake"), meta.audio_keep_alive.Description(), keepAlive);
    BindToggle(keepAlive, &Fields::audio_keep_alive, _refreshers);

    _latencyModeSegment = new Widget::SegmentedControl;
    _latencyModeSegment->AddSegment(tr("Music (AAC)"));
    _latencyModeSegment->AddSegment(tr("Gaming (SBC)"));
    _latencyProfileRow = latency->AddRow(
        tr("Audio mode"), tr("Choose standard AAC audio or low-latency SBC for gaming."),
        _latencyModeSegment);
    connect(
        _latencyModeSegment, &Widget::SegmentedControl::CurrentIndexChanged, this, [this](int idx) {
            if (idx < 0 || idx > 1) {
                return;
            }
            _latencyModeSegment->setEnabled(false);
            _latencyProfileRow->SetDescription(tr("Switching audio mode..."));
            emit LatencyProfileSwitchRequested(idx == 1);
            // Real updates arrive immediately via LatencyDiagnosticsChanged once the native
            // elevated helper completes; this timeout is purely a fallback safety net.
            QTimer::singleShot(15000, this, [this] { RefreshLatencyDiagnostics(); });
        });

    auto *shortcuts = new Widget::SettingsGroup{tr("Global shortcuts"), column};

    auto *hkConnect = new Widget::ComboBox;
    shortcuts->AddRow(
        tr("Connect / disconnect"), meta.hotkey_toggle_connect.Description(), hkConnect);
    BindHotkeyCombo(
        hkConnect, &Fields::hotkey_toggle_connect,
        {QStringLiteral("Ctrl+Alt+A"), QStringLiteral("Ctrl+Alt+C"), QStringLiteral("Ctrl+Shift+A"),
         QStringLiteral("Ctrl+Alt+F9")},
        _refreshers);

    auto *hkPopup = new Widget::ComboBox;
    shortcuts->AddRow(tr("Toggle battery popup"), meta.hotkey_toggle_popup.Description(), hkPopup);
    BindHotkeyCombo(
        hkPopup, &Fields::hotkey_toggle_popup,
        {QStringLiteral("Ctrl+Alt+B"), QStringLiteral("Ctrl+Alt+P"), QStringLiteral("Ctrl+Shift+B"),
         QStringLiteral("Ctrl+Alt+F10")},
        _refreshers);

    auto *hkNoise = new Widget::ComboBox;
    shortcuts->AddRow(
        tr("Cycle noise control"), meta.hotkey_cycle_noise_control.Description(), hkNoise);
    BindHotkeyCombo(
        hkNoise, &Fields::hotkey_cycle_noise_control,
        {QStringLiteral("Ctrl+Alt+N"), QStringLiteral("Ctrl+Shift+N"), QStringLiteral("Ctrl+Alt+M"),
         QStringLiteral("Ctrl+Alt+F11")},
        _refreshers);

    column->layout()->addWidget(audio);
    column->layout()->addWidget(latency);
    column->layout()->addWidget(shortcuts);
    return column;
}

QWidget *SettingsWindow::BuildFeaturesPage()
{
    const auto &meta = GetConstMetaFields();
    auto *column = MakeColumn(this);

    auto *playback = new Widget::SettingsGroup{tr("Playback"), column};

    _earDetection = new Widget::Toggle;
    playback->AddRow(
        tr("Automatic ear detection"), meta.automatic_ear_detection.Description(), _earDetection);
    BindToggle(_earDetection, &Fields::automatic_ear_detection, _refreshers);

    auto *resume = new Widget::Toggle;
    _earDetectionResumeRow = playback->AddRow(
        tr("Resume playback when they are back in"),
        meta.ear_detection_resume_playback.Description(), resume);
    BindToggle(resume, &Fields::ear_detection_resume_playback, _refreshers);

    auto *singleBud = new Widget::Toggle;
    _earDetectionSingleBudRow = playback->AddRow(
        tr("One AirPod is enough"), meta.ear_detection_single_bud.Description(), singleBud);
    BindToggle(singleBud, &Fields::ear_detection_single_bud, _refreshers);

    connect(_earDetection, &QAbstractButton::toggled, this, [this](bool enabled) {
        _earDetectionResumeRow->setEnabled(enabled);
        _earDetectionSingleBudRow->setEnabled(enabled);
    });

    auto *pauseOnDisconnect = new Widget::Toggle;
    playback->AddRow(
        tr("Pause when AirPods disconnect"), meta.pause_on_disconnect.Description(),
        pauseOnDisconnect);
    BindToggle(pauseOnDisconnect, &Fields::pause_on_disconnect, _refreshers);

    auto *battery = new Widget::SettingsGroup{tr("Battery"), column};

    auto *lowBattery = new Widget::Toggle;
    battery->AddRow(
        tr("Low battery notification"), meta.low_battery_notification.Description(), lowBattery);
    BindToggle(lowBattery, &Fields::low_battery_notification, _refreshers);

    auto *threshold = new Widget::SegmentedControl;
    for (const auto level : kLowBatteryLevels) {
        threshold->AddSegment(QStringLiteral("%1%").arg(level));
    }
    battery->AddRow(tr("Low means"), meta.low_battery_threshold.Description(), threshold);
    BindValues(threshold, &Fields::low_battery_threshold, kLowBatteryLevels, _refreshers);

    auto *fullCharge = new Widget::Toggle;
    battery->AddRow(
        tr("Full charge notification"), meta.full_charge_notification.Description(), fullCharge);
    BindToggle(fullCharge, &Fields::full_charge_notification, _refreshers);

    auto *quick = new Widget::SettingsGroup{tr("Quick connect"), column};

    _quickConnectDevice = new Widget::ComboBox;
    _quickConnectDevice->setMinimumWidth(220);
    _quickConnectDeviceRow = quick->AddRow(
        tr("Device"), tr("The paired Bluetooth audio device the two options below connect."),
        _quickConnectDevice);
    connect(
        _quickConnectDevice, qOverload<int>(&QComboBox::currentIndexChanged), this,
        [this](int index) {
            // Repopulating the list (with signals blocked) is the only way to reach -1.
            if (index < 0) {
                return;
            }
            WriteSetting(
                &Fields::tray_quick_connect_device_id,
                _quickConnectDevice->itemData(index).toString());
        });

    _quickConnectEnabled = new Widget::Toggle;
    quick->AddRow(
        tr("Connect on a tray icon click"), meta.tray_quick_connect_enabled.Description(),
        _quickConnectEnabled);
    BindToggle(_quickConnectEnabled, &Fields::tray_quick_connect_enabled, _refreshers);

    auto *autoConnect = new Widget::Toggle;
    quick->AddRow(
        tr("Connect when the case opens"), meta.auto_connect_on_lid_open.Description(),
        autoConnect);
    BindToggle(autoConnect, &Fields::auto_connect_on_lid_open, _refreshers);

    column->layout()->addWidget(playback);
    column->layout()->addWidget(battery);
    column->layout()->addWidget(quick);
    return column;
}

QWidget *SettingsWindow::BuildAboutPage()
{
    auto *column = MakeColumn(this);

    auto *identity = new Widget::SettingsGroup{{}, column};

    auto *header = new QWidget{column};
    auto *headerLayout = new QHBoxLayout{header};
    headerLayout->setContentsMargins(Theme::Metric::RowPaddingH, 16, Theme::Metric::RowPaddingH, 8);
    headerLayout->setSpacing(16);

    auto *icon = new QLabel{header};
    icon->setPixmap(qApp->windowIcon().pixmap(56, 56));
    icon->setFixedSize(56, 56);
    headerLayout->addWidget(icon, 0, Qt::AlignTop);

    // Rebuilt on a change of mode, because the link colour is in the HTML.
    const auto bindHtml = [this](QLabel *label, QString (*html)()) {
        _themeRefreshers.push_back([label, html] { label->setText(html()); });
        _themeRefreshers.back()();
    };

    auto *name = MakeLabel(
        Config::ProgramName, Theme::Font::Heading(), Widget::Label::Role::Primary, header);
    auto *version = MakeLabel({}, Theme::Font::Caption(), Widget::Label::Role::Secondary, header);
    bindHtml(version, VersionHtml);
    version->setOpenExternalLinks(true);
    version->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::TextSelectableByMouse);
    auto *description =
        MakeLabel(Config::Description, Theme::Font::Body(), Widget::Label::Role::Secondary, header);
    description->setWordWrap(true);
    auto *copyright = MakeLabel(
        Config::Copyright, Theme::Font::Footnote(), Widget::Label::Role::Secondary, header);

    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(3);
    text->addWidget(name);
    text->addWidget(version);
    text->addSpacing(4);
    text->addWidget(description);
    text->addWidget(copyright);
    headerLayout->addLayout(text, 1);
    identity->AddWidget(header);

    auto *links = new QWidget{column};
    auto *linksLayout = new QHBoxLayout{links};
    linksLayout->setContentsMargins(Theme::Metric::RowPaddingH, 10, Theme::Metric::RowPaddingH, 14);
    linksLayout->setSpacing(10);

    const auto addLink = [&](const QString &label, const char *url) {
        auto *button = MakeButton(label, links);
        connect(
            button, &QPushButton::clicked, this, [url] { QDesktopServices::openUrl(QUrl{url}); });
        linksLayout->addWidget(button);
    };
    addLink(tr("GitHub"), Config::UrlRepository);
    addLink(tr("Report an issue"), Config::UrlIssues);
    addLink(tr("Licence"), Config::UrlLicense);
    linksLayout->addStretch();

    auto *logs = MakeButton(tr("Open logs folder"), links);
    connect(logs, &QPushButton::clicked, this, [] {
        Utils::File::OpenFileLocation(Logger::GetLogFilePath());
    });
    linksLayout->addWidget(logs);
    identity->AddWidget(links);

    _checkUpdatesButton = MakeButton(tr("Check now"), column);
    const QString updateDesc =
        Utils::File::IsPortableMode()
            ? tr("Running in portable mode (v%1). Updates open the release page.")
                  .arg(CONFIG_VERSION_STRING)
            : tr("Current version v%1.").arg(CONFIG_VERSION_STRING);
    _updateCheckRow = identity->AddRow(tr("Updates"), updateDesc, _checkUpdatesButton);
    connect(_checkUpdatesButton, &QPushButton::clicked, this, [this] {
        _checkUpdatesButton->setEnabled(false);
        _updateCheckRow->SetDescription(tr("Checking GitHub Releases..."));
        emit ManualUpdateCheckRequested();
    });

    auto *diagnostics = new Widget::SettingsGroup{tr("Diagnostics"), column};
    _copyDiagButton = MakeButton(tr("Copy report"), column);
    _diagTransportRow = diagnostics->AddRow(
        tr("Transport & AAP driver"), tr("BLE Broadcast (AAP L2CAP driver not installed)."),
        _copyDiagButton);
    auto *diagSpacer = new QWidget{column};
    diagSpacer->setFixedSize(1, 1);
    _diagConnectionRow = diagnostics->AddRow(tr("Connection & audio"), tr("Idle."), diagSpacer);

    connect(_copyDiagButton, &QPushButton::clicked, this, [this] {
        RefreshDiagnostics();
        QString report;
        if (_diagnosticsProvider) {
            report = _diagnosticsProvider().clipboardReport;
        }
        if (report.isEmpty()) {
            report = QStringLiteral("AirpodsBridge v%1").arg(CONFIG_VERSION_STRING);
        }
        if (auto *cb = QGuiApplication::clipboard(); cb != nullptr) {
            cb->setText(report);
        }
        _copyDiagButton->setText(tr("Copied"));
        QTimer::singleShot(
            1500, _copyDiagButton, [btn = _copyDiagButton] { btn->setText(tr("Copy report")); });
    });

    auto *thirdParty = new Widget::SettingsGroup{tr("Third-party components"), column};

    auto *creditsLabel = MakeLabel({}, Theme::Font::Body(), Widget::Label::Role::Primary, column);
    bindHtml(creditsLabel, CreditsHtml);
    creditsLabel->setOpenExternalLinks(true);
    creditsLabel->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    creditsLabel->setContentsMargins(
        Theme::Metric::RowPaddingH, 12, Theme::Metric::RowPaddingH, 12);
    creditsLabel->setWordWrap(true);
    thirdParty->AddWidget(creditsLabel);

    column->layout()->addWidget(identity);
    column->layout()->addWidget(diagnostics);
    column->layout()->addWidget(thirdParty);
    return column;
}

QWidget *SettingsWindow::BuildDebugPage()
{
    auto *column = MakeColumn(this);
#if defined APB_DEBUG
    auto *group = new Widget::SettingsGroup{"Bluetooth advertisement override", column};

    _advOverride = new Widget::Toggle;
    group->AddRow(
        "Enabled", "One advertisement per line, as hex bytes separated by spaces.", _advOverride);

    _advOverrideText = new QTextEdit;
    _advOverrideText->setFrameShape(QFrame::NoFrame);
    _advOverrideText->setMinimumHeight(180);
    _advOverrideText->setStyleSheet("background: transparent;");
    _advOverrideText->setContentsMargins(
        Theme::Metric::RowPaddingH, 8, Theme::Metric::RowPaddingH, 8);
    group->AddWidget(_advOverrideText);

    connect(_advOverride, &QAbstractButton::toggled, this, &SettingsWindow::UpdateAdvOverride);
    connect(_advOverrideText, &QTextEdit::textChanged, this, &SettingsWindow::UpdateAdvOverride);

    column->layout()->addWidget(group);
#endif
    return column;
}

//////////////////////////////////////////////////

void SettingsWindow::Refresh()
{
    const auto fields = GetCurrent();

    for (const auto &refresh : _refreshers) {
        refresh(fields);
    }

    {
        const QSignalBlocker blocker{_range};
        _range->setValue(-fields.rssi_min);
    }
    _rangeValue->setText(RangeText(-fields.rssi_min));

    {
        const QSignalBlocker blocker{_volumeCapSlider};
        _volumeCapSlider->setValue(std::clamp<int>(
            static_cast<int>(fields.volume_cap_level), kVolumeCapMin, kVolumeCapMax));
    }
    _volumeCapValue->setText(QStringLiteral("%1%").arg(_volumeCapSlider->value()));
    _volumeCapSliderRow->setEnabled(fields.volume_cap_enabled);

    _earDetectionResumeRow->setEnabled(fields.automatic_ear_detection);
    _earDetectionSingleBudRow->setEnabled(fields.automatic_ear_detection);

    RefreshBinding(fields);
    RefreshPreferredMicrophones(fields);
    RefreshQuickConnectDevices(fields);
    RefreshRadioControl();
    RefreshLatencyDiagnostics();
    RefreshDiagnostics();
}

void SettingsWindow::SetAudioEndpointsProvider(AudioEndpointsProvider provider)
{
    _audioEndpointsProvider = std::move(provider);
    RefreshPreferredMicrophones(GetCurrent());
}

void SettingsWindow::SetDiagnosticsProvider(DiagnosticsProvider provider)
{
    _diagnosticsProvider = std::move(provider);
    RefreshDiagnostics();
}

void SettingsWindow::SetLatencyDiagnosticsProvider(LatencyDiagnosticsProvider provider)
{
    _latencyDiagnosticsProvider = std::move(provider);
    RefreshLatencyDiagnostics();
}

void SettingsWindow::SetRadioControlProvider(RadioControlProvider provider)
{
    _radioControlProvider = std::move(provider);
    RefreshRadioControl();
}

void SettingsWindow::SetRadioSetupBusy(bool busy, const QString &statusText)
{
    if (_radioPrimaryButton == nullptr || _radioStatusRow == nullptr) {
        return;
    }
    _radioPrimaryButton->setEnabled(!busy);
    if (_radioRestoreButton != nullptr) {
        _radioRestoreButton->setEnabled(!busy);
    }
    if (busy) {
        _radioPrimaryButton->setText(tr("Working..."));
        if (_radioProgressContainer != nullptr && _radioProgressBar != nullptr) {
            _radioProgressBar->SetProgress(5);
            _radioProgressContainer->show();
        }
        if (!statusText.isEmpty()) {
            _radioStatusRow->SetDescription(statusText);
        }
    }
    else {
        if (_radioProgressContainer != nullptr) {
            _radioProgressContainer->hide();
        }
        RefreshRadioControl();
        if (!statusText.isEmpty()) {
            _radioStatusRow->SetDescription(statusText);
        }
    }
}

void SettingsWindow::SetRadioSetupProgress(int percent, const QString &statusText)
{
    if (_radioProgressContainer != nullptr && _radioProgressBar != nullptr) {
        _radioProgressContainer->show();
        _radioProgressBar->SetProgress(percent);
    }
    if (_radioStatusRow != nullptr && !statusText.isEmpty()) {
        _radioStatusRow->SetDescription(statusText);
    }
}

void SettingsWindow::RefreshRadioControl()
{
    if (_radioStatusRow == nullptr) {
        return;
    }
    _radioPrimaryButton->setEnabled(true);

    RadioControlInfo info;
    if (_radioControlProvider) {
        info = _radioControlProvider();
    }

    if (!info.engineDetail.isEmpty()) {
        _radioStatusRow->SetDescription(info.engineDetail);
    }
    if (!info.adapterSummary.isEmpty()) {
        _radioAdapterRow->SetDescription(info.adapterSummary);
    }

    if (info.guardTripped) {
        _radioPrimaryAction = RadioServiceAction::RetryGuard;
        _radioPrimaryButton->setText(tr("Retry engine"));
        _radioPrimaryButton->SetPrimary(true);
    }
    else if (info.serviceInstalled && info.serviceRunning) {
        _radioPrimaryAction = RadioServiceAction::Uninstall;
        _radioPrimaryButton->setText(tr("Disable"));
        _radioPrimaryButton->SetPrimary(false);
    }
    else {
        _radioPrimaryAction = RadioServiceAction::InstallOrEnable;
        _radioPrimaryButton->setText(tr("Enable"));
        _radioPrimaryButton->SetPrimary(true);
    }
    _radioRestoreButton->setEnabled(info.serviceInstalled || info.serviceRunning);

    const bool live = info.aapConnected && info.state.has_value();
    const auto caps =
        Core::AirPods::CapabilitiesFor(live ? info.state->model : Core::AirPods::Model::Unknown);

    _aapNoiseModeContainer->setEnabled(live && caps.SupportsNoiseControl());
    _aapNoiseControl->setEnabled(live && caps.SupportsNoiseControl());
    _aapConvAwarenessRow->setEnabled(live && caps.supportsConversationalAwareness);
    _aapOneBudAncRow->setEnabled(live && caps.supportsNoiseCancellation);
    _aapPersonalizedVolRow->setEnabled(live && caps.supportsAdaptiveAudio);
    _aapLoudSoundRow->setEnabled(live && caps.supportsAdaptiveAudio);
    _aapAllowOffRow->setEnabled(live && caps.supportsAllowOffMode);
    _aapHeartRateRow->setEnabled(live && caps.supportsHeartRate);
    _aapHeartRateCard->setEnabled(live && caps.supportsHeartRate);
    _aapMicSideRow->setEnabled(live && !caps.isOnePiece);
    _aapPressSpeedRow->setEnabled(live && !caps.isOnePiece);
    _aapPressHoldRow->setEnabled(live && !caps.isOnePiece);
    _aapToneVolumeRow->setEnabled(live);
    _aapHeadGesturesRow->setEnabled(live && caps.supportsAdaptiveAudio);
    _aapHearingAidRow->setEnabled(live && caps.supportsHearingAid);
    _aapMediaAssistRow->setEnabled(live && caps.supportsHearingAid);

    if (live) {
        const auto &nc = info.state->noiseControl;
        {
            const QSignalBlocker b{_aapNoiseControl};
            NoiseControlPresentation pres;
            pres.visible = true;
            pres.activeMode = nc.activeMode.value_or(Core::AirPods::NoiseControlMode::Off);
            pres.supportsAdaptive = caps.supportsAdaptiveAudio;
            pres.adaptiveNoiseLevel = nc.adaptiveNoiseLevel.value_or(50);
            pres.allowOffListeningMode = nc.allowOffListeningMode.value_or(false);
            _aapNoiseControl->ApplyPresentation(pres);
        }
        {
            const QSignalBlocker b{_aapConvAwareness};
            _aapConvAwareness->setChecked(nc.conversationalAwareness.value_or(false));
        }
        {
            const QSignalBlocker b{_aapOneBudAnc};
            _aapOneBudAnc->setChecked(nc.oneBudAnc.value_or(false));
        }
        {
            const QSignalBlocker b{_aapPersonalizedVol};
            _aapPersonalizedVol->setChecked(nc.personalizedVolume.value_or(false));
        }
        {
            const QSignalBlocker b{_aapLoudSound};
            _aapLoudSound->setChecked(nc.loudSoundReduction.value_or(false));
        }
        {
            const QSignalBlocker b{_aapAllowOff};
            _aapAllowOff->setChecked(nc.allowOffListeningMode.value_or(false));
        }
        {
            const QSignalBlocker b{_aapHeartRate};
            _aapHeartRate->setChecked(nc.heartRateMonitoringEnabled.value_or(false));
        }
        const QString modelDisplay = info.state->displayName.isEmpty()
                                         ? Helper::ToString(info.state->model)
                                         : info.state->displayName;
        _aapHeartRateCard->SetTelemetry(Widget::HeartRateCard::Telemetry{
            .supported = caps.supportsHeartRate,
            .sensorEnabled = nc.heartRateMonitoringEnabled.value_or(false),
            .connected = true,
            .deviceName = modelDisplay,
            .status = nc.heartRateStatus,
            .bpm = nc.heartRateBpm,
            .confidence = nc.heartRateConfidence,
            .minBpm = nc.heartRateMinBpm,
            .maxBpm = nc.heartRateMaxBpm,
            .avgBpm = nc.heartRateAvgBpm,
            .history = nc.heartRateHistory,
        });
        if (_aapHeartRateTelemetryRow != nullptr) {
            if (!caps.supportsHeartRate) {
                _aapHeartRateTelemetryRow->SetDescription(
                    tr("%1 does not include a heart rate sensor.").arg(modelDisplay));
            }
            else if (info.heartRateServiceId.has_value()) {
                _aapHeartRateTelemetryRow->SetDescription(
                    tr("%1 %2 Optical sensor ready").arg(modelDisplay).arg(QChar{0x00B7}));
            }
            else {
                _aapHeartRateTelemetryRow->SetDescription(tr("%1 %2 Initializing optical sensor...")
                                                              .arg(modelDisplay)
                                                              .arg(QChar{0x00B7}));
            }
        }
        {
            const QSignalBlocker b{_aapMicSide};
            _aapMicSide->SetCurrentIndex(
                static_cast<int>(nc.microphoneSide.value_or(Core::AirPods::MicrophoneSide::Auto)));
        }
        {
            const QSignalBlocker b{_aapPressSpeed};
            _aapPressSpeed->SetCurrentIndex(
                static_cast<int>(nc.pressSpeed.value_or(Core::AirPods::PressSpeed::Default)));
        }
        {
            const QSignalBlocker b{_aapPressHold};
            _aapPressHold->SetCurrentIndex(static_cast<int>(
                nc.pressHoldDuration.value_or(Core::AirPods::PressHoldDuration::Default)));
        }
        {
            const QSignalBlocker b{_aapToneVolumeSlider};
            const int vol = std::clamp<int>(static_cast<int>(nc.toneVolume.value_or(100)), 15, 100);
            _aapToneVolumeSlider->setValue(vol);
            _aapToneVolumeValue->setText(QStringLiteral("%1%").arg(vol));
        }
        {
            const QSignalBlocker b{_aapHeadGestures};
            _aapHeadGestures->setChecked(nc.headGestures.value_or(false));
        }
        {
            const QSignalBlocker b{_aapHearingAid};
            _aapHearingAid->setChecked(nc.hearingAidEnabled.value_or(false));
        }
        {
            const QSignalBlocker b{_aapMediaAssist};
            _aapMediaAssist->setChecked(nc.mediaAssistEnabled.value_or(false));
        }
    }
    else {
        NoiseControlPresentation idlePres;
        idlePres.visible = true;
        idlePres.activeMode = Core::AirPods::NoiseControlMode::NoiseCancellation;
        idlePres.supportsAdaptive = true;
        idlePres.adaptiveNoiseLevel = 50;
        _aapNoiseControl->ApplyPresentation(idlePres);
        _aapHeartRateCard->SetTelemetry(Widget::HeartRateCard::Telemetry{
            .supported = false,
            .sensorEnabled = false,
            .connected = false,
        });
        if (_aapHeartRateTelemetryRow != nullptr) {
            _aapHeartRateTelemetryRow->SetDescription(
                tr("Connect AirPods Pro 3 or Powerbeats Pro 2 to measure."));
        }
    }
}

void SettingsWindow::RefreshAudioDevices()
{
    RefreshPreferredMicrophones(GetCurrent());
}

void SettingsWindow::RefreshLatencyDiagnostics()
{
    if (_latencyProfileRow == nullptr || _latencyModeSegment == nullptr) {
        return;
    }
    if (!_latencyDiagnosticsProvider) {
        _latencyModeSegment->setEnabled(true);
        return;
    }

    const auto diag = _latencyDiagnosticsProvider();
    {
        const QSignalBlocker blocker{_latencyModeSegment};
        _latencyModeSegment->SetCurrentIndex(diag.aacEnabled ? 0 : 1);
    }

    if (diag.applyingProfile) {
        _latencyModeSegment->setEnabled(false);
        _latencyProfileRow->SetDescription(tr("Switching audio mode..."));
        return;
    }

    _latencyModeSegment->setEnabled(true);
    if (diag.aacEnabled) {
        _latencyProfileRow->SetDescription(tr("High-quality AAC active."));
    }
    else if (diag.enginePeriodMs > 0.0 && diag.enginePeriodMs <= 5.0) {
        _latencyProfileRow->SetDescription(
            QStringLiteral("%1 %2 %3")
                .arg(tr("Low-latency SBC active"))
                .arg(QChar{0x00B7})
                .arg(tr("%1 ms engine").arg(diag.enginePeriodMs, 0, 'f', 1)));
    }
    else {
        _latencyProfileRow->SetDescription(tr("Low-latency SBC active."));
    }
}

void SettingsWindow::RefreshDiagnostics()
{
    if (!_diagnosticsProvider || _diagTransportRow == nullptr || _diagConnectionRow == nullptr) {
        return;
    }
    const auto info = _diagnosticsProvider();
    if (!info.transportSummary.isEmpty()) {
        _diagTransportRow->SetDescription(info.transportSummary);
    }
    if (!info.connectionSummary.isEmpty()) {
        _diagConnectionRow->SetDescription(info.connectionSummary);
    }
}

void SettingsWindow::RefreshPreferredMicrophones(const Fields &fields)
{
    if (_preferredMic == nullptr) {
        return;
    }
    const QSignalBlocker blocker{_preferredMic};
    _preferredMic->clear();
    _preferredMic->addItem(tr("Windows default (off)"), QString{});

    if (_audioEndpointsProvider) {
        for (const auto &ep : _audioEndpointsProvider()) {
            if (ep.isBluetoothHfp) {
                continue;
            }
            _preferredMic->addItem(ep.name, ep.id);
        }
    }

    const QString &selected = fields.preferred_mic_device_id;
    int idx = _preferredMic->findData(selected);
    if (idx < 0 && !selected.isEmpty()) {
        _preferredMic->addItem(tr("Saved microphone (not available)"), selected);
        idx = _preferredMic->count() - 1;
    }
    _preferredMic->setCurrentIndex(idx >= 0 ? idx : 0);
}

void SettingsWindow::SetUpdateCheckFeedback(
    Core::Update::CheckStatus status, const QString &version)
{
    if (_checkUpdatesButton != nullptr) {
        _checkUpdatesButton->setEnabled(true);
    }
    if (_updateCheckRow == nullptr) {
        return;
    }

    switch (status) {
    case Core::Update::CheckStatus::UpToDate:
        _updateCheckRow->SetDescription(tr("You are up to date (v%1).").arg(CONFIG_VERSION_STRING));
        break;
    case Core::Update::CheckStatus::UpdateAvailable:
        _updateCheckRow->SetDescription(
            version.isEmpty() ? tr("A newer version is available.")
                              : tr("Version v%1 is available.").arg(version));
        break;
    case Core::Update::CheckStatus::Failed:
        _updateCheckRow->SetDescription(tr("Could not reach GitHub Releases. Try again later."));
        break;
    }
}

void SettingsWindow::RefreshBindingState()
{
    RefreshBinding(GetCurrent());
}

void SettingsWindow::RefreshBinding(const Fields &fields)
{
    const bool bound = fields.device_address != 0;
    const QString name = fields.bound_device_name.trimmed();
    if (bound) {
        _bindingRow->SetDescription(
            name.isEmpty()
                ? tr("Battery and ear state follow this device. Unbind it to choose another.")
                : tr("%1 %2 battery and ear state follow this device.")
                      .arg(name, QStringLiteral("\u2014")));
    }
    else {
        _bindingRow->SetDescription(
            tr("No device is bound. Click Bind to choose your paired AirPods."));
    }
    _bindButton->setText(bound ? tr("Change") : tr("Bind"));
    _unbind->setEnabled(bound);
}

void SettingsWindow::RefreshQuickConnectDevices(const Fields &fields)
{
    // Repopulating emits index changes that must not be written back as choices.
    const QSignalBlocker blocker{_quickConnectDevice};

    const auto &selectedId = fields.tray_quick_connect_device_id;

    _quickConnectDevice->clear();
    for (const auto &device : _quickConnect.Devices()) {
        _quickConnectDevice->addItem(device.name, device.id);
    }

    auto selectedIndex = _quickConnectDevice->findData(selectedId);
    if (selectedIndex == -1 && !selectedId.isEmpty()) {
        // The saved device is paired but not reporting an audio endpoint right now - switched
        // off, out of range, or the enumeration has not finished. Show it as chosen but
        // unavailable rather than an empty box that reads like nothing was ever configured.
        _quickConnectDevice->addItem(tr("Saved device (not available)"), selectedId);
        selectedIndex = _quickConnectDevice->count() - 1;
    }
    _quickConnectDevice->setCurrentIndex(selectedIndex);

    // The device serves both connect options, so it is only ever gated on there being one.
    _quickConnectDeviceRow->setEnabled(_quickConnectDevice->count() > 0);
}

void SettingsWindow::RestoreDefaults()
{
    const auto answer = QMessageBox::question(
        this, Config::ProgramName,
        tr("Restore every setting to its default?\n\n"
           "Your bound AirPods and the quick connect device are kept."),
        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Yes) {
        return;
    }

    const auto current = GetCurrent();
    auto defaults = GetDefault();
    defaults.device_address = current.device_address;
    defaults.bound_device_name = current.bound_device_name;
    defaults.tray_quick_connect_device_id = current.tray_quick_connect_device_id;
    Save(std::move(defaults));

    Refresh();
}

void SettingsWindow::UpdateAdvOverride()
{
#if defined APB_DEBUG
    const auto lines = _advOverrideText->toPlainText().split('\n', QString::SkipEmptyParts);

    std::vector<std::vector<uint8_t>> advs;
    for (const auto &line : lines) {
        std::vector<uint8_t> bytes;
        bool lineValid = true;
        for (const auto &byteText : line.split(' ', QString::SkipEmptyParts)) {
            bool success{false};
            const auto byte = byteText.toUInt(&success, 16);
            if (!success || byte > 0xFF) {
                lineValid = false;
                break;
            }
            bytes.emplace_back(static_cast<uint8_t>(byte));
        }
        if (lineValid && !bytes.empty()) {
            advs.emplace_back(std::move(bytes));
        }
    }

    Core::Debug::DebugConfig::GetInstance().UpdateAdvOverride(
        _advOverride->isChecked(), std::move(advs));
#endif
}

bool SettingsWindow::event(QEvent *event)
{
    if (event->type() == Theme::ChangeEvent()) {
        for (const auto &refresh : _themeRefreshers) {
            refresh();
        }
    }
    return QDialog::event(event);
}

void SettingsWindow::showEvent(QShowEvent *event)
{
    QDialog::showEvent(event);

    Refresh();

    // Enumerating paired Bluetooth devices and every audio endpoint is not free, so it
    // happens when the window is opened rather than on every application start.
    _quickConnect.RefreshDevices();
}

} // namespace Gui
