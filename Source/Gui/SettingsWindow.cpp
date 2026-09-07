// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/SettingsWindow.h"

#include <array>
#include <cstdlib>

#include <QDesktopServices>
#include <QEvent>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QSignalBlocker>
#include <QUrl>
#include <QVBoxLayout>

#include <Config.h>

#include "Source/Core/Debug.h"
#include "Source/Gui/Widget/ComboBox.h"
#include "Source/Gui/Widget/Label.h"
#include "Source/Gui/Widget/PushButton.h"
#include "Source/Gui/Widget/ScrollArea.h"
#include "Source/Gui/Widget/SegmentedControl.h"
#include "Source/Gui/Widget/SettingsCard.h"
#include "Source/Gui/Widget/Sidebar.h"
#include "Source/Gui/Widget/Slider.h"
#include "Source/Gui/Widget/Toggle.h"
#include "Source/Presentation/Theme.h"
#include "Source/Support/Logger.h"

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

    auto *appearance = new Widget::SettingsGroup{tr("Appearance"), column};

    auto *theme = new Widget::SegmentedControl;
    theme->AddSegment(tr("System"));
    theme->AddSegment(tr("Light"));
    theme->AddSegment(tr("Dark"));
    appearance->AddRow(tr("Theme"), meta.appearance.Description(), theme);
    BindSegments(theme, &Fields::appearance, _refreshers);

    auto *device = new Widget::SettingsGroup{tr("AirPods"), column};

    _unbind = MakeButton(tr("Unbind"), column);
    _bindingRow = device->AddRow(tr("Bound device"), {}, _unbind);
    connect(_unbind, &QPushButton::clicked, this, [this] {
        WriteSetting(&Fields::device_address, uint64_t{0});
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

    _earDetectionResumeRow->setEnabled(fields.automatic_ear_detection);
    _earDetectionSingleBudRow->setEnabled(fields.automatic_ear_detection);

    RefreshBinding(fields);
    RefreshQuickConnectDevices(fields);
}

void SettingsWindow::RefreshBinding(const Fields &fields)
{
    const bool bound = fields.device_address != 0;
    _bindingRow->SetDescription(
        bound ? tr("Battery and ear state follow this device. Unbind it to choose another.")
              : tr("No device is bound. Open the popup from the tray icon and choose "
                   "“Bind to AirPods”."));
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
        for (const auto &byteText : line.split(' ', QString::SkipEmptyParts)) {
            bool success{false};
            const auto byte = byteText.toUInt(&success, 16);
            APB_ASSERT(success);
            bytes.emplace_back(static_cast<uint8_t>(byte));
        }
        advs.emplace_back(std::move(bytes));
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
