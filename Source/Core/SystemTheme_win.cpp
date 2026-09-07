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

// Where Windows keeps the switch. 1 is light, 0 is dark, and absent -- on builds that
// predate the setting -- means light, which is what those builds are.
constexpr auto kPersonalizeKey =
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr auto kAppsUseLightTheme = "AppsUseLightTheme";

// The broadcast that follows a change to any of the colour settings. `lParam` names the
// area; this is the one the mode belongs to.
constexpr auto kImmersiveColorSet = L"ImmersiveColorSet";

} // namespace

bool IsDark()
{
    const QSettings personalize{QString::fromLatin1(kPersonalizeKey), QSettings::NativeFormat};
    return personalize.value(QString::fromLatin1(kAppsUseLightTheme), 1).toInt() == 0;
}

//
// The native filter sees every message a Qt-owned window receives, which is why the
// hidden window exists: the broadcast goes to top-level windows only, and the application
// may have none of its own on screen when the user flips the switch.
//
class Watcher::Impl final : public QAbstractNativeEventFilter
{
public:
    explicit Impl(Callback onChanged)
        : _onChanged{std::move(onChanged)}, _dark{SystemTheme::IsDark()}
    {
        _receiver.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        _receiver.setGeometry(0, 0, 1, 1);
        _receiver.create(); // realises the native window; it is never shown
        QCoreApplication::instance()->installNativeEventFilter(this);

        LOG(Info, "SystemTheme: Windows apps are {}.", _dark ? "dark" : "light");
    }

    ~Impl() override
    {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }

    bool IsDark() const
    {
        return _dark;
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, long *result) override
    {
        Q_UNUSED(eventType);
        Q_UNUSED(result);

        const auto *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_SETTINGCHANGE || msg->lParam == 0 ||
            std::wcscmp(reinterpret_cast<const wchar_t *>(msg->lParam), kImmersiveColorSet) != 0)
        {
            return false;
        }

        const bool dark = SystemTheme::IsDark();
        if (dark != _dark) {
            _dark = dark;
            LOG(Info, "SystemTheme: Windows apps are now {}.", dark ? "dark" : "light");
            _onChanged(dark);
        }
        return false; // observed, not consumed
    }

private:
    Callback _onChanged;
    bool _dark;
    QWindow _receiver;
};

Watcher::Watcher(Callback onChanged) : _impl{std::make_unique<Impl>(std::move(onChanged))} {}

Watcher::~Watcher() = default;

bool Watcher::IsDark() const
{
    return _impl->IsDark();
}

} // namespace Core::SystemTheme
