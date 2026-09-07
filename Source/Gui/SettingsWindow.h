// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <functional>
#include <vector>

#include <QDialog>
#include <QLabel>
#include <QStackedWidget>
#include <QTextEdit>

#include "Source/Core/QuickConnect.h"
#include "Source/Core/Settings.h"
#include "Source/Gui/Utils.h"

namespace Gui {

namespace Widget {
class ComboBox;
class PushButton;
class SegmentedControl;
class SettingsRow;
class Sidebar;
class Slider;
class Toggle;
} // namespace Widget

//
// The settings window: a sidebar of pages, each a column of cards.
//
// Every control is bound to one field of `Core::Settings::Fields` at construction. A
// change writes through `ModifiableAccess`, which saves and applies it; `Refresh` pushes
// the stored values back into the controls with their signals blocked, so loading never
// echoes as a write. Adding a setting is one row and one `Bind` call.
//
class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    enum class Section : size_t { General, Display, Features, About, Debug };

    explicit SettingsWindow(
        Core::QuickConnect::Controller &quickConnect, QWidget *parent = nullptr);

    Section CurrentSection() const;
    void SelectSection(Section section);

private:
    using Refresher = std::function<void(const Core::Settings::Fields &)>;

    Core::QuickConnect::Controller &_quickConnect;
    std::vector<Refresher> _refreshers;
    // Rich text with colours written into it, rebuilt when the mode changes.
    std::vector<std::function<void()>> _themeRefreshers;

    Widget::Sidebar *_sidebar;
    QStackedWidget *_pages;
    std::array<int, 5> _pageIndex;

    // Controls with state that `Refresh` has to set by hand.
    Widget::Slider *_range{nullptr};
    QLabel *_rangeValue{nullptr};
    Widget::SettingsRow *_bindingRow{nullptr};
    Widget::PushButton *_unbind{nullptr};
    Widget::Toggle *_earDetection{nullptr};
    Widget::SettingsRow *_earDetectionResumeRow{nullptr};
    Widget::SettingsRow *_earDetectionSingleBudRow{nullptr};
    Widget::Toggle *_quickConnectEnabled{nullptr};
    Widget::SettingsRow *_quickConnectDeviceRow{nullptr};
    Widget::ComboBox *_quickConnectDevice{nullptr};
#if defined APB_DEBUG
    Widget::Toggle *_advOverride{nullptr};
    QTextEdit *_advOverrideText{nullptr};
#endif

    QWidget *BuildGeneralPage();
    QWidget *BuildDisplayPage();
    QWidget *BuildFeaturesPage();
    QWidget *BuildAboutPage();
    QWidget *BuildDebugPage();
    QWidget *WrapPage(const QString &title, QWidget *content);

    void Refresh();
    void RefreshBinding(const Core::Settings::Fields &fields);
    void RefreshQuickConnectDevices(const Core::Settings::Fields &fields);
    void RestoreDefaults();
    void UpdateAdvOverride();

    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};

} // namespace Gui
