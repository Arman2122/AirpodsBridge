// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/RegistryStore.h"
#include "Service/Win/WinHandles.h"

#include <algorithm>
#include <cwctype>

namespace Service::Win {

namespace {

HKEY OpenRoot(REGSAM access, bool create)
{
    HKEY key = nullptr;
    LONG rc = ERROR_SUCCESS;
    if (create) {
        rc = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE, RegistryStore::kRootKey, 0, nullptr, REG_OPTION_NON_VOLATILE,
            access | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    }
    else {
        rc = RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, RegistryStore::kRootKey, 0, access | KEY_WOW64_64KEY, &key);
    }
    return rc == ERROR_SUCCESS ? key : nullptr;
}

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (std::towlower(a[i]) != std::towlower(b[i])) {
            return false;
        }
    }
    return true;
}

std::optional<DWORD> ReadDword(HKEY key, const wchar_t *name)
{
    DWORD value = 0;
    DWORD type = 0;
    DWORD size = sizeof(value);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size) ==
            ERROR_SUCCESS &&
        type == REG_DWORD)
    {
        return value;
    }
    return std::nullopt;
}

void WriteDword(HKEY key, const wchar_t *name, DWORD value)
{
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
}

} // namespace

std::optional<std::wstring> RegistryStore::GetStoredRadioInstance()
{
    HKEY key = OpenRoot(KEY_READ, false);
    if (key == nullptr) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    std::optional<std::wstring> out;
    if (RegQueryValueExW(key, L"RadioInstanceId", nullptr, &type, nullptr, &bytes) ==
            ERROR_SUCCESS &&
        type == REG_SZ && bytes >= sizeof(wchar_t))
    {
        std::wstring val(bytes / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(
                key, L"RadioInstanceId", nullptr, &type, reinterpret_cast<LPBYTE>(val.data()),
                &bytes) == ERROR_SUCCESS)
        {
            while (!val.empty() && val.back() == L'\0') {
                val.pop_back();
            }
            if (!val.empty()) {
                out = std::move(val);
            }
        }
    }
    RegCloseKey(key);
    return out;
}

void RegistryStore::SetStoredRadioInstance(const std::optional<std::wstring> &instanceId)
{
    HKEY key = OpenRoot(KEY_WRITE, true);
    if (key == nullptr) {
        return;
    }
    if (!instanceId.has_value() || instanceId->empty()) {
        RegDeleteValueW(key, L"RadioInstanceId");
    }
    else {
        const DWORD bytes = static_cast<DWORD>((instanceId->size() + 1) * sizeof(wchar_t));
        RegSetValueExW(
            key, L"RadioInstanceId", 0, REG_SZ, reinterpret_cast<const BYTE *>(instanceId->c_str()),
            bytes);
    }
    RegCloseKey(key);
}

std::optional<std::string> RegistryStore::GetStoredAdapterName()
{
    HKEY key = OpenRoot(KEY_READ, false);
    if (key == nullptr) {
        return std::nullopt;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    std::optional<std::string> out;
    if (RegQueryValueExW(key, L"AdapterDisplayName", nullptr, &type, nullptr, &bytes) ==
            ERROR_SUCCESS &&
        type == REG_SZ && bytes >= sizeof(wchar_t))
    {
        std::wstring val(bytes / sizeof(wchar_t), L'\0');
        if (RegQueryValueExW(
                key, L"AdapterDisplayName", nullptr, &type, reinterpret_cast<LPBYTE>(val.data()),
                &bytes) == ERROR_SUCCESS)
        {
            while (!val.empty() && val.back() == L'\0') {
                val.pop_back();
            }
            if (!val.empty()) {
                out = WideToUtf8(val);
            }
        }
    }
    RegCloseKey(key);
    return out;
}

void RegistryStore::SetStoredAdapterName(const std::string &displayName)
{
    if (displayName.empty()) {
        return;
    }
    HKEY key = OpenRoot(KEY_WRITE, true);
    if (key == nullptr) {
        return;
    }
    const std::wstring wide = Utf8ToWide(displayName);
    const DWORD bytes = static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t));
    RegSetValueExW(
        key, L"AdapterDisplayName", 0, REG_SZ, reinterpret_cast<const BYTE *>(wide.c_str()), bytes);
    RegCloseKey(key);
}

std::vector<std::wstring> RegistryStore::GetBoundInstances()
{
    std::vector<std::wstring> out;
    HKEY key = OpenRoot(KEY_READ, false);
    if (key == nullptr) {
        return out;
    }
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, L"BoundInstances", nullptr, &type, nullptr, &bytes) ==
            ERROR_SUCCESS &&
        type == REG_MULTI_SZ && bytes >= sizeof(wchar_t))
    {
        std::vector<wchar_t> buf(bytes / sizeof(wchar_t) + 2, L'\0');
        if (RegQueryValueExW(
                key, L"BoundInstances", nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()),
                &bytes) == ERROR_SUCCESS)
        {
            const wchar_t *p = buf.data();
            while (*p != L'\0') {
                std::wstring item{p};
                p += item.size() + 1;
                if (!item.empty()) {
                    out.push_back(std::move(item));
                }
            }
        }
    }
    RegCloseKey(key);
    return out;
}

void RegistryStore::RememberBoundInstance(const std::wstring &instanceId)
{
    auto list = GetBoundInstances();
    for (const auto &existing : list) {
        if (EqualsIgnoreCase(existing, instanceId)) {
            return;
        }
    }
    list.push_back(instanceId);

    std::vector<wchar_t> multi;
    for (const auto &item : list) {
        multi.insert(multi.end(), item.begin(), item.end());
        multi.push_back(L'\0');
    }
    multi.push_back(L'\0');

    HKEY key = OpenRoot(KEY_WRITE, true);
    if (key == nullptr) {
        return;
    }
    RegSetValueExW(
        key, L"BoundInstances", 0, REG_MULTI_SZ, reinterpret_cast<const BYTE *>(multi.data()),
        static_cast<DWORD>(multi.size() * sizeof(wchar_t)));
    RegCloseKey(key);
}

void RegistryStore::ClearBoundInstances()
{
    HKEY key = OpenRoot(KEY_WRITE, true);
    if (key == nullptr) {
        return;
    }
    RegDeleteValueW(key, L"BoundInstances");
    RegCloseKey(key);
}

bool RegistryStore::GetGuardTripped()
{
    HKEY key = OpenRoot(KEY_READ, false);
    if (key == nullptr) {
        return false;
    }
    const bool v = ReadDword(key, L"GuardTripped").value_or(0) != 0;
    RegCloseKey(key);
    return v;
}

void RegistryStore::SetGuardTripped(bool tripped)
{
    HKEY key = OpenRoot(KEY_WRITE, true);
    if (key == nullptr) {
        return;
    }
    WriteDword(key, L"GuardTripped", tripped ? 1u : 0u);
    RegCloseKey(key);
}

bool RegistryStore::GetUninstalling()
{
    HKEY key = OpenRoot(KEY_READ, false);
    if (key == nullptr) {
        return false;
    }
    const bool v = ReadDword(key, L"Uninstalling").value_or(0) != 0;
    RegCloseKey(key);
    return v;
}

void RegistryStore::SetUninstalling(bool uninstalling)
{
    HKEY key = OpenRoot(KEY_WRITE, true);
    if (key == nullptr) {
        return;
    }
    WriteDword(key, L"Uninstalling", uninstalling ? 1u : 0u);
    RegCloseKey(key);
}

Protocol::ProxyConfig RegistryStore::LoadProxyConfig()
{
    Protocol::ProxyConfig cfg{};
    HKEY key = OpenRoot(KEY_READ, false);
    if (key == nullptr) {
        return cfg;
    }
    cfg.lowLatencyQos = ReadDword(key, L"LowLatencyQos").value_or(1) != 0;
    const uint32_t savedPollUs = ReadDword(key, L"PollLatencyUs").value_or(25000);
    // Migrate legacy sub-25 ms (3.75 ms / 11.25 ms) poll settings that injected mid-frame Master
    // NULL polls inside the 23.22 ms A2DP audio cadence and starved Apple H2's primary-to-
    // secondary inter-bud relay during bud removal/reinsertion.
    cfg.pollLatencyUs = (savedPollUs < 25000 || savedPollUs > 50000) ? 25000u : savedPollUs;
    cfg.declineHfp = ReadDword(key, L"DeclineHfp").value_or(0) != 0;
    cfg.virtualMicEnabled = ReadDword(key, L"VirtualMicEnabled").value_or(0) != 0;
    RegCloseKey(key);
    if (savedPollUs != cfg.pollLatencyUs) {
        SaveProxyConfig(cfg);
    }
    return cfg;
}

void RegistryStore::SaveProxyConfig(const Protocol::ProxyConfig &cfg)
{
    HKEY key = OpenRoot(KEY_WRITE, true);
    if (key == nullptr) {
        return;
    }
    WriteDword(key, L"LowLatencyQos", cfg.lowLatencyQos ? 1u : 0u);
    WriteDword(key, L"PollLatencyUs", cfg.pollLatencyUs);
    WriteDword(key, L"DeclineHfp", cfg.declineHfp ? 1u : 0u);
    WriteDword(key, L"VirtualMicEnabled", cfg.virtualMicEnabled ? 1u : 0u);
    RegCloseKey(key);
}

} // namespace Service::Win
