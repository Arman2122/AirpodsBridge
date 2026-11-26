// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QString>

namespace Core::Notifications {

// Registers the process AppUserModelID (and ensures a Start Menu shortcut carries it when
// not running in portable mode) so Windows 10/11 WinRT toast notifications can be delivered.
void Initialize();

// Attempts to show a native Windows toast notification with optional local image path.
// Returns false when unavailable (e.g. portable mode or WinRT toast service refused), so
// the caller can fall back cleanly to `QSystemTrayIcon::showMessage`.
bool ShowToast(const QString &title, const QString &body, const QString &imageFilePath = {});

} // namespace Core::Notifications
