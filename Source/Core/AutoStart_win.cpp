// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/Core/AutoStart.h"

#include <QCoreApplication>
#include <QDir>
#include <QSettings>

#include <Config.h>

namespace Core::AutoStart {

namespace {

class WindowsService final : public Service
{
public:
    void SetEnabled(bool enabled) override
    {
        QSettings registry{
            "HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run",
            QSettings::Registry64Format};
        const auto executable = QString{"\"%1\""}.arg(
            QDir::toNativeSeparators(QCoreApplication::applicationFilePath()));

        if (enabled) {
            registry.setValue(Config::ProgramName, executable);
        }
        else {
            registry.remove(Config::ProgramName);
        }
    }
};

} // namespace

std::unique_ptr<Service> CreateAutoStartService()
{
    return std::make_unique<WindowsService>();
}

} // namespace Core::AutoStart
