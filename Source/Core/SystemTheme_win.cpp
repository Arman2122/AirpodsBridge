// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/SystemTheme.h"

#include <cwchar>

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QCoreApplication>
#include <QSettings>
#include <QWindow>

#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"

namespace Core::SystemTheme {

namespace {

// Where Windows keeps the switches. 1 is light, 0 is dark. Absent `AppsUseLightTheme`
// means light; absent `SystemUsesLightTheme` (older Windows 10) means dark taskbar.
constexpr auto kPersonalizeKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr auto kAppsUseLightTheme = "AppsUseLightTheme";
constexpr auto kSystemUsesLightTheme = "SystemUsesLightTheme";

// The broadcast that follows a change to any of the colour settings. `lParam` names the
// area; this is the one the mode belongs to.
constexpr auto kImmersiveColorSet = L"ImmersiveColorSet";

} // namespace

bool IsDark()
{
    const QSettings personalize{QString::fromLatin1(kPersonalizeKey), QSettings::NativeFormat};
    return personalize.value(QString::fromLatin1(kAppsUseLightTheme), 1).toInt() == 0;
}

bool IsTaskbarDark()
{
    const QSettings personalize{QString::fromLatin1(kPersonalizeKey), QSettings::NativeFormat};
    return personalize.value(QString::fromLatin1(kSystemUsesLightTheme), 0).toInt() == 0;
}

//
// The native filter sees every message a Qt-owned window receives, which is why the
// hidden window exists: the broadcast goes to top-level windows only, and the application
// may have none of its own on screen when the user flips the switch.
//
class Watcher::Impl final : public QAbstractNativeEventFilter
{
public:
    Impl(Callback onAppsChanged, Callback onTaskbarChanged)
        : _onAppsChanged{std::move(onAppsChanged)}, _onTaskbarChanged{std::move(onTaskbarChanged)},
          _dark{SystemTheme::IsDark()}, _taskbarDark{SystemTheme::IsTaskbarDark()}
    {
        _receiver.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        _receiver.setGeometry(0, 0, 1, 1);
        _receiver.create(); // realises the native window; it is never shown
        QCoreApplication::instance()->installNativeEventFilter(this);

        LOG(Info, "SystemTheme: Windows apps are {}, taskbar is {}.", _dark ? "dark" : "light",
            _taskbarDark ? "dark" : "light");
    }

    ~Impl() override
    {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }

    bool IsDark() const
    {
        return _dark;
    }

    bool IsTaskbarDark() const
    {
        return _taskbarDark;
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override
    {
        Q_UNUSED(eventType);
        Q_UNUSED(result);

        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_SETTINGCHANGE || msg->wParam != 0 || msg->lParam == 0 ||
            std::wcscmp(reinterpret_cast<const wchar_t *>(msg->lParam), kImmersiveColorSet) != 0)
        {
            return false;
        }

        const bool dark = SystemTheme::IsDark();
        if (dark != _dark) {
            _dark = dark;
            LOG(Info, "SystemTheme: Windows apps are now {}.", dark ? "dark" : "light");
            if (_onAppsChanged) {
                _onAppsChanged(dark);
            }
        }

        const bool taskbarDark = SystemTheme::IsTaskbarDark();
        if (taskbarDark != _taskbarDark) {
            _taskbarDark = taskbarDark;
            LOG(Info, "SystemTheme: Windows taskbar is now {}.", taskbarDark ? "dark" : "light");
            if (_onTaskbarChanged) {
                _onTaskbarChanged(taskbarDark);
            }
        }

        return false; // observed, not consumed
    }

private:
    Callback _onAppsChanged;
    Callback _onTaskbarChanged;
    bool _dark;
    bool _taskbarDark;
    QWindow _receiver;
};

Watcher::Watcher(Callback onAppsChanged, Callback onTaskbarChanged)
    : _impl{std::make_unique<Impl>(std::move(onAppsChanged), std::move(onTaskbarChanged))}
{
}

Watcher::~Watcher() = default;

bool Watcher::IsDark() const
{
    return _impl->IsDark();
}

bool Watcher::IsTaskbarDark() const
{
    return _impl->IsTaskbarDark();
}

} // namespace Core::SystemTheme
