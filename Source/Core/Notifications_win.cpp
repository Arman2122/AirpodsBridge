// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/Notifications.h"

#include <objbase.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>

#include <atomic>
#include <filesystem>

#include <QCoreApplication>
#include <QDir>

#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"
#include "Source/Support/Utils.h"

namespace Core::Notifications {

namespace {

constexpr wchar_t kAppUserModelId[] = L"Arman2122.AirpodsBridge";
std::atomic<bool> gToastsReady{false};

QString EscapeXml(QString text)
{
    text.replace('&', QStringLiteral("&amp;"));
    text.replace('<', QStringLiteral("&lt;"));
    text.replace('>', QStringLiteral("&gt;"));
    text.replace('"', QStringLiteral("&quot;"));
    text.replace('\'', QStringLiteral("&apos;"));
    return text;
}

bool EnsureStartMenuShortcut()
{
    PWSTR appDataPath = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &appDataPath)) ||
        appDataPath == nullptr)
    {
        return false;
    }

    const std::filesystem::path shortcutPath =
        std::filesystem::path{appDataPath} / L"AirpodsBridge.lnk";
    CoTaskMemFree(appDataPath);

    if (std::filesystem::exists(shortcutPath)) {
        return true;
    }

    Support::OS::Windows::Com::UniquePtr<IShellLinkW> shellLink;
    if (FAILED(CoCreateInstance(
            CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, shellLink.GetIID(),
            reinterpret_cast<void **>(shellLink.ReleaseAndAddressOf()))) ||
        !shellLink)
    {
        return false;
    }

    const std::wstring exePath =
        QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    if (FAILED(shellLink->SetPath(exePath.c_str()))) {
        return false;
    }

    Support::OS::Windows::Com::UniquePtr<IPropertyStore> propStore;
    if (FAILED(shellLink->QueryInterface(
            propStore.GetIID(), reinterpret_cast<void **>(propStore.ReleaseAndAddressOf()))) ||
        !propStore)
    {
        return false;
    }

    PROPVARIANT aumidVar;
    if (FAILED(InitPropVariantFromString(kAppUserModelId, &aumidVar))) {
        return false;
    }
    const HRESULT setHr = propStore->SetValue(PKEY_AppUserModel_ID, aumidVar);
    PropVariantClear(&aumidVar);
    if (FAILED(setHr) || FAILED(propStore->Commit())) {
        return false;
    }

    Support::OS::Windows::Com::UniquePtr<IPersistFile> persistFile;
    if (FAILED(shellLink->QueryInterface(
            persistFile.GetIID(), reinterpret_cast<void **>(persistFile.ReleaseAndAddressOf()))) ||
        !persistFile)
    {
        return false;
    }

    return SUCCEEDED(persistFile->Save(shortcutPath.c_str(), TRUE));
}

} // namespace

void Initialize()
{
    if (Utils::File::IsPortableMode()) {
        // Portable mode promises zero footprint outside the application directory.
        gToastsReady.store(false);
        return;
    }

    if (FAILED(SetCurrentProcessExplicitAppUserModelID(kAppUserModelId))) {
        return;
    }

    gToastsReady.store(EnsureStartMenuShortcut());
}

bool ShowToast(const QString &title, const QString &body, const QString &imageFilePath)
{
    if (!gToastsReady.load()) {
        return false;
    }

    try {
        using namespace winrt::Windows::Data::Xml::Dom;
        using namespace winrt::Windows::UI::Notifications;

        QString imageTag;
        if (!imageFilePath.isEmpty()) {
            imageTag = QStringLiteral("<image placement=\"appLogoOverride\" src=\"%1\"/>")
                           .arg(EscapeXml(imageFilePath));
        }

        const QString xml =
            QStringLiteral("<toast><visual><binding template=\"ToastGeneric\">%1<text>%2</text>"
                           "<text>%3</text></binding></visual></toast>")
                .arg(imageTag, EscapeXml(title), EscapeXml(body));

        XmlDocument doc;
        doc.LoadXml(xml.toStdWString());
        ToastNotification toast{doc};
        ToastNotificationManager::CreateToastNotifier(kAppUserModelId).Show(toast);
        return true;
    }
    catch (const winrt::hresult_error &ex) {
        LOG(Warn, "Notifications: WinRT toast failed (hr={:#x}), falling back to tray balloon.",
            static_cast<uint32_t>(ex.code()));
        return false;
    }
    catch (...) {
        return false;
    }
}

} // namespace Core::Notifications
