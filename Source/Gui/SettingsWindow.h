// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <QLabel>
#include <QDialog>
#include <QSlider>
#include <QCheckBox>

#include "Source/Core/QuickConnect.h"
#include "Source/Core/Settings.h"
#include "Source/Gui/Utils.h"

#include "ui_SettingsWindow.h"

namespace Gui {

using namespace Core::Settings;

class SettingsWindow : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsWindow(
        Core::QuickConnect::Controller &quickConnect, QWidget *parent = nullptr);

    int GetTabCount() const;
    int GetTabCurrentIndex() const;
    int GetTabLastVisibleIndex() const;
    void SetTabIndex(int index);

private:
    Ui::SettingsWindow _ui;
    bool _trigger{true};
    Core::QuickConnect::Controller &_quickConnect;

    void InitCreditsText();
    void RestoreDefaults();
    void Update(const Fields &fields, bool trigger);
    void UpdateQuickConnectDevices(const Fields &fields);
    void UpdateAdvOverride();

    void showEvent(QShowEvent *event) override;

    // General
    void On_cbAutoRun_toggled(bool checked);
    void On_pbUnbind_clicked();

    // Visual
    void On_cbDisplayBatteryOnTrayIcon_toggled(TrayIconBatteryBehavior behavior);
    void On_cbDisplayBatteryOnTaskbar_toggled(TaskbarStatusBehavior behavior);

    // Features
    void On_cbTrayQuickConnectEnabled_toggled(bool checked);
    void On_cbTrayQuickConnectDevice_currentIndexChanged(int index);
    void On_cbAutoEarDetection_toggled(bool checked);
    void On_hsMaxReceivingRange_valueChanged(int value);

    // About
    void On_pbOpenLogsDirectory_clicked();

    // Debug
    void On_cbAdvOverride_toggled(bool checked);
    void On_teAdvOverride_textChanged();

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};
} // namespace Gui
