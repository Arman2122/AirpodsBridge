// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "Service/Protocol/ServiceProtocol.h"

namespace Service::Win {

//
// Persistent HKLM configuration & rollback metadata under
// `HKLM\SOFTWARE\AirpodsBridge\RadioService`.
//
class RegistryStore
{
public:
    static constexpr wchar_t kRootKey[] = L"SOFTWARE\\AirpodsBridge\\RadioService";

    static std::optional<std::wstring> GetStoredRadioInstance();
    static void SetStoredRadioInstance(const std::optional<std::wstring> &instanceId);

    static std::optional<std::string> GetStoredAdapterName();
    static void SetStoredAdapterName(const std::string &displayName);

    static std::vector<std::wstring> GetBoundInstances();
    static void RememberBoundInstance(const std::wstring &instanceId);
    static void ClearBoundInstances();

    static bool GetGuardTripped();
    static void SetGuardTripped(bool tripped);

    static bool GetUninstalling();
    static void SetUninstalling(bool uninstalling);

    static Protocol::ProxyConfig LoadProxyConfig();
    static void SaveProxyConfig(const Protocol::ProxyConfig &cfg);
};

} // namespace Service::Win
