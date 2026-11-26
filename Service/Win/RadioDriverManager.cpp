// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/RadioDriverManager.h"
#include "Service/Win/RegistryStore.h"
#include "Service/Win/ServiceLog.h"
#include "Service/Win/WinHandles.h"

#include <cfgmgr32.h>
#include <devpkey.h>
#include <initguid.h>
#include <newdev.h>
#include <regstr.h>
#include <winioctl.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <regex>

namespace Service::Win {

namespace {

constexpr DWORD kIoctlUsbHubCyclePort = 0x00220444u;
struct UsbCyclePortParams {
    ULONG connectionIndex;
    ULONG statusReturned;
};

// Standard Windows device interface & setup class GUIDs.
// GUID_BTHPORT_DEVICE_INTERFACE
constexpr GUID kRadioInterfaceGuid = {
    0x0850302A, 0xB344, 0x4FDA, {0x9B, 0xE9, 0x90, 0x57, 0x6B, 0x8D, 0x46, 0xF0}};
// GUID_DEVCLASS_USBDevice (WinUSB inbox class)
constexpr GUID kUsbDeviceClassGuid = {
    0x88BAE032, 0x5A81, 0x49F0, {0xBC, 0x3D, 0xA4, 0xFF, 0x13, 0x82, 0x16, 0xD6}};
// GUID_DEVINTERFACE_USB_HUB
constexpr GUID kUsbHubInterfaceGuid = {
    0xF18A0E88, 0xC30C, 0x11D0, {0x88, 0x15, 0x00, 0xA0, 0xC9, 0x06, 0xBE, 0xD8}};

constexpr wchar_t kUsbDeviceInterfaceStr[] = L"{a5dcbf10-6530-11d2-901f-00c04fb951ed}";
// Custom WinUSB interface GUID assigned to composite USB Bluetooth functions (&MI_nn) because
// the USB hub only publishes GUID_DEVINTERFACE_USB_DEVICE for whole devices.
constexpr wchar_t kCompositeFunctionInterfaceStr[] = L"{9e4a7c31-52b8-4d6e-8c1a-3f7e2d90b415}";
constexpr wchar_t kAapServicePrefix[] = L"BTHENUM\\{74EC2172-0BAD-4D01-8F77-997B2BE0722A}";
constexpr wchar_t kEnumRoot[] = L"SYSTEM\\CurrentControlSet\\Enum";

bool StartsWithIgnoreCase(std::wstring_view s, std::wstring_view prefix)
{
    if (s.size() < prefix.size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (std::towlower(s[i]) != std::towlower(prefix[i])) {
            return false;
        }
    }
    return true;
}

bool ContainsIgnoreCase(std::wstring_view haystack, std::wstring_view needle)
{
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    for (size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < needle.size(); ++j) {
            if (std::towlower(haystack[i + j]) != std::towlower(needle[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            return true;
        }
    }
    return false;
}

bool EqualsIgnoreCase(std::wstring_view a, std::wstring_view b)
{
    return a.size() == b.size() && StartsWithIgnoreCase(a, b);
}

std::wstring GetDevNodeService(DEVINST dev)
{
    wchar_t buf[256]{};
    ULONG size = sizeof(buf);
    if (CM_Get_DevNode_Registry_PropertyW(dev, CM_DRP_SERVICE, nullptr, buf, &size, 0) ==
        CR_SUCCESS)
    {
        return std::wstring{buf};
    }
    return {};
}

std::wstring GetDevNodeDisplayName(DEVINST dev)
{
    wchar_t buf[256]{};
    ULONG size = sizeof(buf);
    if (CM_Get_DevNode_Registry_PropertyW(dev, CM_DRP_FRIENDLYNAME, nullptr, buf, &size, 0) ==
            CR_SUCCESS &&
        buf[0] != L'\0')
    {
        return std::wstring{buf};
    }
    size = sizeof(buf);
    if (CM_Get_DevNode_Registry_PropertyW(dev, CM_DRP_DEVICEDESC, nullptr, buf, &size, 0) ==
            CR_SUCCESS &&
        buf[0] != L'\0')
    {
        return std::wstring{buf};
    }
    return {};
}

std::wstring GetDevNodeId(DEVINST dev)
{
    wchar_t buf[MAX_DEVICE_ID_LEN + 1]{};
    if (CM_Get_Device_IDW(dev, buf, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS) {
        return std::wstring{buf};
    }
    return {};
}

std::vector<std::wstring>
EnumerateDeviceInterfaces(const GUID &guid, const wchar_t *deviceId = nullptr)
{
    ULONG chars = 0;
    GUID g = guid;
    if (CM_Get_Device_Interface_List_SizeW(
            &chars, &g, const_cast<DEVINSTID_W>(deviceId), CM_GET_DEVICE_INTERFACE_LIST_PRESENT) !=
            CR_SUCCESS ||
        chars <= 1)
    {
        return {};
    }
    std::vector<wchar_t> buf(chars, L'\0');
    if (CM_Get_Device_Interface_ListW(
            &g, const_cast<DEVINSTID_W>(deviceId), buf.data(), chars,
            CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS)
    {
        return {};
    }
    std::vector<std::wstring> result;
    const wchar_t *p = buf.data();
    while (*p != L'\0') {
        std::wstring s{p};
        p += s.size() + 1;
        if (!s.empty()) {
            result.push_back(std::move(s));
        }
    }
    return result;
}

std::optional<std::wstring> GetInterfaceInstanceId(const std::wstring &ifacePath)
{
    DEVPROPTYPE type = 0;
    wchar_t buf[MAX_DEVICE_ID_LEN + 1]{};
    ULONG size = sizeof(buf);
    if (CM_Get_Device_Interface_PropertyW(
            ifacePath.c_str(), &DEVPKEY_Device_InstanceId, &type, reinterpret_cast<PBYTE>(buf),
            &size, 0) == CR_SUCCESS &&
        type == DEVPROP_TYPE_STRING)
    {
        return std::wstring{buf};
    }
    return std::nullopt;
}

bool IsUsbipVirtualDevice(const std::wstring &instanceId)
{
    DEVINST dev = 0;
    if (CM_Locate_DevNodeW(
            &dev, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
        CR_SUCCESS)
    {
        return false;
    }
    for (int depth = 0; depth < 6; ++depth) {
        DEVINST parent = 0;
        if (CM_Get_Parent(&parent, dev, 0) != CR_SUCCESS) {
            break;
        }
        if (EqualsIgnoreCase(GetDevNodeService(parent), L"usbip2_ude")) {
            return true;
        }
        dev = parent;
    }
    return false;
}

bool HasDescendantWithPrefix(DEVINST dev, std::wstring_view prefix, int maxDepth)
{
    if (maxDepth <= 0) {
        return false;
    }
    DEVINST child = 0;
    if (CM_Get_Child(&child, dev, 0) != CR_SUCCESS) {
        return false;
    }
    do {
        if (StartsWithIgnoreCase(GetDevNodeId(child), prefix) ||
            HasDescendantWithPrefix(child, prefix, maxDepth - 1))
        {
            return true;
        }
    } while (CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);
    return false;
}

bool IsAudioProfileDevNode(std::wstring_view instanceId)
{
    if (!StartsWithIgnoreCase(instanceId, L"BTHENUM\\{") || instanceId.size() < 17) {
        return false;
    }
    constexpr std::wstring_view kAudioUuids[] = {
        L"0000110a", L"0000110b",              // A2DP source, sink
        L"0000110c", L"0000110e", L"0000110f", // AVRCP
        L"0000111e", L"0000111f",              // Hands-Free
        L"00001108", L"00001112", L"00001131", // Headset
    };
    const auto uuid8 = instanceId.substr(9, 8);
    for (auto u : kAudioUuids) {
        if (EqualsIgnoreCase(uuid8, u)) {
            return true;
        }
    }
    return false;
}

bool InstallDriver(const std::wstring &instanceId, bool toWinUsb, bool &rebootRequired)
{
    rebootRequired = false;

    UniqueDevInfo set{SetupDiCreateDeviceInfoList(nullptr, nullptr)};
    if (!set.Valid()) {
        ServiceLog::Write(
            LogLevel::Error, "driver",
            "SetupDiCreateDeviceInfoList failed: " + FormatWin32Error(GetLastError()));
        return false;
    }

    SP_DEVINFO_DATA dev{};
    dev.cbSize = sizeof(dev);
    if (!SetupDiOpenDeviceInfoW(set.Get(), instanceId.c_str(), nullptr, 0, &dev)) {
        ServiceLog::Write(
            LogLevel::Error, "driver",
            "SetupDiOpenDeviceInfoW failed for " + WideToUtf8(instanceId) + ": " +
                FormatWin32Error(GetLastError()));
        return false;
    }

    SP_DEVINSTALL_PARAMS_W params{};
    params.cbSize = sizeof(params);
    if (!SetupDiGetDeviceInstallParamsW(set.Get(), &dev, &params)) {
        ServiceLog::Write(
            LogLevel::Error, "driver",
            "SetupDiGetDeviceInstallParamsW failed: " + FormatWin32Error(GetLastError()));
        return false;
    }

    DWORD driverType = SPDIT_COMPATDRIVER;
    if (toWinUsb) {
        wchar_t infPath[MAX_PATH]{};
        if (ExpandEnvironmentStringsW(L"%SystemRoot%\\INF\\winusb.inf", infPath, MAX_PATH) == 0) {
            return false;
        }
        params.Flags |= DI_ENUMSINGLEINF;
        params.FlagsEx |= DI_FLAGSEX_ALLOWEXCLUDEDDRVS;
        wcsncpy_s(params.DriverPath, infPath, _TRUNCATE);
        driverType = SPDIT_CLASSDRIVER;

        // WinUSB belongs to setup class USBDevice; switch the devnode's class GUID first so
        // SetupDiBuildDriverInfoList enumerates WinUSB's inbox class driver entries cleanly.
        constexpr wchar_t kUsbClassStr[] = L"{88bae032-5a81-49f0-bc3d-a4ff138216d6}";
        if (!SetupDiSetDeviceRegistryPropertyW(
                set.Get(), &dev, SPDRP_CLASSGUID, reinterpret_cast<const BYTE *>(kUsbClassStr),
                sizeof(kUsbClassStr)))
        {
            ServiceLog::Write(
                LogLevel::Error, "driver",
                "SetupDiSetDeviceRegistryPropertyW(SPDRP_CLASSGUID) failed: " +
                    FormatWin32Error(GetLastError()));
            return false;
        }
        dev.ClassGuid = kUsbDeviceClassGuid;
    }

    if (!SetupDiSetDeviceInstallParamsW(set.Get(), &dev, &params) ||
        !SetupDiBuildDriverInfoList(set.Get(), &dev, driverType))
    {
        ServiceLog::Write(
            LogLevel::Error, "driver",
            "SetupDiBuildDriverInfoList failed: " + FormatWin32Error(GetLastError()));
        return false;
    }

    if (toWinUsb) {
        bool found = false;
        SP_DRVINFO_DATA_W chosen{};
        for (DWORD idx = 0;; ++idx) {
            SP_DRVINFO_DATA_W di{};
            di.cbSize = sizeof(di);
            if (!SetupDiEnumDriverInfoW(set.Get(), &dev, driverType, idx, &di)) {
                break;
            }
            if (ContainsIgnoreCase(di.Description, L"WinUsb")) {
                chosen = di;
                found = true;
                break;
            }
        }
        if (!found || !SetupDiSetSelectedDriverW(set.Get(), &dev, &chosen)) {
            ServiceLog::Write(
                LogLevel::Error, "driver",
                "Failed to select inbox WinUsb Device driver from winusb.inf.");
            return false;
        }
    }
    else {
        if (!SetupDiCallClassInstaller(DIF_SELECTBESTCOMPATDRV, set.Get(), &dev)) {
            ServiceLog::Write(
                LogLevel::Error, "driver",
                "DIF_SELECTBESTCOMPATDRV failed: " + FormatWin32Error(GetLastError()));
            return false;
        }
    }

    SP_DRVINFO_DATA_W selected{};
    selected.cbSize = sizeof(selected);
    BOOL needReboot = FALSE;
    if (!SetupDiGetSelectedDriverW(set.Get(), &dev, &selected) ||
        !DiInstallDevice(nullptr, set.Get(), &dev, &selected, 0, &needReboot))
    {
        ServiceLog::Write(
            LogLevel::Error, "driver",
            "DiInstallDevice failed: " + FormatWin32Error(GetLastError()));
        return false;
    }

    rebootRequired = (needReboot != FALSE);
    ServiceLog::Write(
        LogLevel::Info, "driver",
        "Installed \"" + WideToUtf8(selected.Description) + "\" on " + WideToUtf8(instanceId) +
            " (rebootNeeded=" + (rebootRequired ? "true" : "false") + ").");
    return true;
}

} // namespace

bool RadioDriverManager::IsUsbipDriverReady()
{
    // Verify the Microsoft-signed `usbip2_ude` kernel driver service is installed and either
    // running or startable, and that `usbip.exe` is available.
    UniqueScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!scm.Valid()) {
        return false;
    }
    UniqueScHandle svc{
        OpenServiceW(scm.Get(), L"usbip2_ude", SERVICE_QUERY_STATUS | SERVICE_QUERY_CONFIG)};
    if (!svc.Valid()) {
        return false;
    }
    return FindUsbipExecutable().has_value();
}

std::optional<std::wstring> RadioDriverManager::FindUsbipExecutable()
{
    std::vector<std::filesystem::path> candidates;

    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) > 0) {
        const auto dir = std::filesystem::path{exePath}.parent_path();
        candidates.push_back(dir / L"usbip.exe");
        candidates.push_back(dir / L"usbip" / L"usbip.exe");
    }

    wchar_t progFiles[MAX_PATH]{};
    if (ExpandEnvironmentStringsW(L"%ProgramFiles%", progFiles, MAX_PATH) > 0) {
        const std::filesystem::path pf{progFiles};
        candidates.push_back(pf / L"USBip" / L"usbip.exe");
        candidates.push_back(pf / L"usbip-win2" / L"usbip.exe");
    }

    std::error_code ec;
    for (const auto &p : candidates) {
        if (std::filesystem::is_regular_file(p, ec)) {
            return p.wstring();
        }
    }
    return std::nullopt;
}

bool RadioDriverManager::ParseVidPid(
    const std::wstring &instanceId, uint16_t &vidOut, uint16_t &pidOut)
{
    vidOut = 0;
    pidOut = 0;
    static const std::wregex kRe{
        L"VID_([0-9A-Fa-f]{4})&PID_([0-9A-Fa-f]{4})", std::regex_constants::icase};
    std::wsmatch m;
    if (!std::regex_search(instanceId, m, kRe)) {
        return false;
    }
    vidOut = static_cast<uint16_t>(std::stoul(m[1].str(), nullptr, 16));
    pidOut = static_cast<uint16_t>(std::stoul(m[2].str(), nullptr, 16));
    return true;
}

AdapterSearchResult RadioDriverManager::FindPhysicalAdapter()
{
    auto buildCandidate = [](const std::wstring &id) -> AdapterCandidate {
        AdapterCandidate c{};
        c.instanceId = id;
        c.isCompositeFunction = IsCompositeFunction(id);
        c.onWinUsb = IsOnWinUsb(id);
        ParseVidPid(id, c.vid, c.pid);
        DEVINST dev = 0;
        if (CM_Locate_DevNodeW(
                &dev, const_cast<DEVINSTID_W>(id.c_str()), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS)
        {
            c.displayName = WideToUtf8(GetDevNodeDisplayName(dev));
        }
        return c;
    };

    // 1. Reuse previously stored physical radio if still present.
    if (const auto stored = RegistryStore::GetStoredRadioInstance();
        stored.has_value() && IsPresent(*stored) && !IsUsbipVirtualDevice(*stored))
    {
        return AdapterSearchResult{
            .status = AdapterSearchStatus::Found,
            .adapter = buildCandidate(*stored),
        };
    }

    // 2. Any previously WinUSB-bound radio that is present (Windows won't list it as a BT radio).
    for (const auto &bound : RegistryStore::GetBoundInstances()) {
        if (IsPresent(bound) && IsOnWinUsb(bound) && !IsUsbipVirtualDevice(bound)) {
            return AdapterSearchResult{
                .status = AdapterSearchStatus::Found,
                .adapter = buildCandidate(bound),
            };
        }
    }

    // 3. Scan live physical Bluetooth radios.
    std::vector<std::wstring> usbRadios;
    bool sawNonUsb = false;
    for (const auto &iface : EnumerateDeviceInterfaces(kRadioInterfaceGuid)) {
        const auto idOpt = GetInterfaceInstanceId(iface);
        if (!idOpt.has_value()) {
            continue;
        }
        const std::wstring &id = *idOpt;
        if (IsUsbipVirtualDevice(id)) {
            continue;
        }
        if (!StartsWithIgnoreCase(id, L"USB\\VID_")) {
            ServiceLog::Write(
                LogLevel::Info, "driver", "Ignoring non-USB Bluetooth radio: " + WideToUtf8(id));
            sawNonUsb = true;
            continue;
        }
        usbRadios.push_back(id);
    }

    if (usbRadios.empty()) {
        return AdapterSearchResult{
            .status = sawNonUsb ? AdapterSearchStatus::NonUsbOnly : AdapterSearchStatus::NoAdapter,
            .adapter = std::nullopt,
        };
    }

    // Prefer whole USB devices over composite functions, and prefer any radio that already has
    // an AirPods AAP service node paired beneath it.
    std::sort(usbRadios.begin(), usbRadios.end(), [](const std::wstring &a, const std::wstring &b) {
        return IsCompositeFunction(a) < IsCompositeFunction(b);
    });

    for (const auto &id : usbRadios) {
        DEVINST dev = 0;
        if (CM_Locate_DevNodeW(
                &dev, const_cast<DEVINSTID_W>(id.c_str()), CM_LOCATE_DEVNODE_NORMAL) ==
                CR_SUCCESS &&
            HasDescendantWithPrefix(dev, kAapServicePrefix, 4))
        {
            return AdapterSearchResult{
                .status = AdapterSearchStatus::Found,
                .adapter = buildCandidate(id),
            };
        }
    }

    return AdapterSearchResult{
        .status = AdapterSearchStatus::Found,
        .adapter = buildCandidate(usbRadios.front()),
    };
}

bool RadioDriverManager::IsPresent(const std::wstring &instanceId)
{
    DEVINST dev = 0;
    return CM_Locate_DevNodeW(
               &dev, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) ==
           CR_SUCCESS;
}

bool RadioDriverManager::IsOnWinUsb(const std::wstring &instanceId)
{
    DEVINST dev = 0;
    if (CM_Locate_DevNodeW(
            &dev, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
        CR_SUCCESS)
    {
        return false;
    }
    return EqualsIgnoreCase(GetDevNodeService(dev), L"WinUSB");
}

bool RadioDriverManager::NeedsRestart(const std::wstring &instanceId)
{
    DEVINST dev = 0;
    ULONG status = 0;
    ULONG problem = 0;
    return CM_Locate_DevNodeW(
               &dev, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) ==
               CR_SUCCESS &&
           CM_Get_DevNode_Status(&status, &problem, dev, 0) == CR_SUCCESS &&
           (status & DN_NEED_RESTART) != 0;
}

bool RadioDriverManager::IsCompositeFunction(const std::wstring &instanceId)
{
    return ContainsIgnoreCase(instanceId, L"&MI_");
}

std::wstring RadioDriverManager::DevicePathForWinUsb(const std::wstring &instanceId)
{
    std::wstring path = L"\\\\?\\" + instanceId;
    std::replace(path.begin() + 4, path.end(), L'\\', L'#');
    path += L'#';
    path +=
        IsCompositeFunction(instanceId) ? kCompositeFunctionInterfaceStr : kUsbDeviceInterfaceStr;
    return path;
}

std::string RadioDriverManager::DescribeDeviceState(const std::wstring &instanceId)
{
    DEVINST dev = 0;
    if (CM_Locate_DevNodeW(
            &dev, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
        CR_SUCCESS)
    {
        return "not present";
    }
    ULONG status = 0;
    ULONG problem = 0;
    if (CM_Get_DevNode_Status(&status, &problem, dev, 0) != CR_SUCCESS) {
        return "status unreadable";
    }
    char buf[160]{};
    const std::string svc = WideToUtf8(GetDevNodeService(dev));
    std::snprintf(
        buf, sizeof(buf), "status=0x%08lX problem=%lu service=%s", status, problem,
        svc.empty() ? "none" : svc.c_str());
    return std::string{buf};
}

bool RadioDriverManager::BindWinUsb(const std::wstring &instanceId, bool &rebootRequired)
{
    RegistryStore::RememberBoundInstance(instanceId);

    if (IsCompositeFunction(instanceId)) {
        // Assign a dedicated WinUSB device interface GUID for composite USB functions.
        const std::wstring paramKey =
            std::wstring{kEnumRoot} + L"\\" + instanceId + L"\\Device Parameters";
        HKEY key = nullptr;
        if (RegCreateKeyExW(
                HKEY_LOCAL_MACHINE, paramKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
                KEY_WRITE | KEY_WOW64_64KEY, nullptr, &key, nullptr) == ERROR_SUCCESS)
        {
            std::vector<wchar_t> multi;
            const std::wstring_view guidView{kCompositeFunctionInterfaceStr};
            multi.insert(multi.end(), guidView.begin(), guidView.end());
            multi.push_back(L'\0');
            multi.push_back(L'\0');
            RegSetValueExW(
                key, L"DeviceInterfaceGUIDs", 0, REG_MULTI_SZ,
                reinterpret_cast<const BYTE *>(multi.data()),
                static_cast<DWORD>(multi.size() * sizeof(wchar_t)));
            RegCloseKey(key);
        }
    }

    const bool ok = InstallDriver(instanceId, true, rebootRequired);
    if (ok && (rebootRequired || NeedsRestart(instanceId))) {
        ServiceLog::Write(
            LogLevel::Info, "driver",
            "Adapter needs restart after WinUSB bind; cycling USB hub port.");
        if (CycleHubPort(instanceId)) {
            rebootRequired = false;
        }
    }
    return ok;
}

bool RadioDriverManager::RestoreNative(const std::wstring &instanceId, bool &rebootRequired)
{
    if (!IsPresent(instanceId)) {
        MarkForReinstall(instanceId);
        rebootRequired = false;
        return true;
    }
    const bool ok = InstallDriver(instanceId, false, rebootRequired);
    if (ok && (rebootRequired || NeedsRestart(instanceId))) {
        if (CycleHubPort(instanceId)) {
            rebootRequired = false;
        }
    }
    return ok;
}

bool RadioDriverManager::CycleHubPort(const std::wstring &instanceId)
{
    DEVINST dev = 0;
    if (CM_Locate_DevNodeW(
            &dev, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
        CR_SUCCESS)
    {
        return false;
    }

    if (IsCompositeFunction(instanceId)) {
        DEVINST composite = 0;
        if (CM_Get_Parent(&composite, dev, 0) == CR_SUCCESS) {
            dev = composite;
        }
    }

    DEVINST hub = 0;
    if (CM_Get_Parent(&hub, dev, 0) != CR_SUCCESS) {
        return false;
    }
    const std::wstring hubId = GetDevNodeId(hub);
    if (hubId.empty()) {
        return false;
    }

    ULONG port = 0;
    ULONG size = sizeof(port);
    if (CM_Get_DevNode_Registry_PropertyW(dev, CM_DRP_ADDRESS, nullptr, &port, &size, 0) !=
            CR_SUCCESS ||
        port == 0)
    {
        return false;
    }

    for (const auto &hubPath : EnumerateDeviceInterfaces(kUsbHubInterfaceGuid, hubId.c_str())) {
        UniqueHandle h{CreateFileW(
            hubPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
            OPEN_EXISTING, 0, nullptr)};
        if (!h.Valid()) {
            continue;
        }
        UsbCyclePortParams params{};
        params.connectionIndex = port;
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(
            h.Get(), kIoctlUsbHubCyclePort, &params, sizeof(params), &params, sizeof(params),
            &returned, nullptr);
        ServiceLog::Write(
            ok ? LogLevel::Info : LogLevel::Warn, "driver",
            "IOCTL_USB_HUB_CYCLE_PORT on " + WideToUtf8(instanceId) + " port " +
                std::to_string(port) +
                (ok ? " succeeded." : " failed: " + FormatWin32Error(GetLastError())));
        return ok != FALSE;
    }
    return false;
}

bool RadioDriverManager::MarkForReinstall(const std::wstring &instanceId)
{
    DEVINST dev = 0;
    if (CM_Locate_DevNodeW(
            &dev, const_cast<DEVINSTID_W>(instanceId.c_str()), CM_LOCATE_DEVNODE_PHANTOM) !=
        CR_SUCCESS)
    {
        return false;
    }
    ULONG flags = 0;
    ULONG size = sizeof(flags);
    CM_Get_DevNode_Registry_PropertyW(dev, CM_DRP_CONFIGFLAGS, nullptr, &flags, &size, 0);
    flags |= CONFIGFLAG_REINSTALL;
    return CM_Set_DevNode_Registry_PropertyW(dev, CM_DRP_CONFIGFLAGS, &flags, sizeof(flags), 0) ==
           CR_SUCCESS;
}

void RadioDriverManager::RestoreAllBoundAdapters()
{
    for (const auto &id : RegistryStore::GetBoundInstances()) {
        bool reboot = false;
        if (IsPresent(id)) {
            if (IsOnWinUsb(id)) {
                RestoreNative(id, reboot);
            }
        }
        else {
            MarkForReinstall(id);
        }
    }
}

uint32_t RadioDriverManager::ForgetHiddenVirtualAudioNodes(const std::wstring &physicalInstanceId)
{
    // Locate sibling instances of the same USB VID/PID (virtual instances created by usbip-win2)
    // and remove only phantom audio profile devnodes so native endpoints keep clean names.
    const size_t slash = physicalInstanceId.rfind(L'\\');
    if (slash == std::wstring::npos) {
        return 0;
    }
    std::wstring deviceKey = physicalInstanceId.substr(0, slash);
    if (const size_t miPos = deviceKey.find(L"&MI_"); miPos != std::wstring::npos) {
        deviceKey = deviceKey.substr(0, miPos);
    }

    uint32_t removed = 0;
    const std::wstring enumBthPath = std::wstring{kEnumRoot} + L"\\BTHENUM";
    HKEY bthKey = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, enumBthPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &bthKey) !=
        ERROR_SUCCESS)
    {
        return 0;
    }

    wchar_t devName[256]{};
    for (DWORD i = 0;; ++i) {
        DWORD nameLen = 256;
        if (RegEnumKeyExW(bthKey, i, devName, &nameLen, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS)
        {
            break;
        }
        const std::wstring prefix = std::wstring{L"BTHENUM\\"} + devName;
        if (!IsAudioProfileDevNode(prefix)) {
            continue;
        }
        HKEY subKey = nullptr;
        if (RegOpenKeyExW(bthKey, devName, 0, KEY_READ | KEY_WOW64_64KEY, &subKey) != ERROR_SUCCESS)
        {
            continue;
        }
        wchar_t instName[256]{};
        for (DWORD j = 0;; ++j) {
            DWORD instLen = 256;
            if (RegEnumKeyExW(subKey, j, instName, &instLen, nullptr, nullptr, nullptr, nullptr) !=
                ERROR_SUCCESS)
            {
                break;
            }
            const std::wstring fullId = prefix + L"\\" + instName;
            DEVINST dev = 0;
            if (CM_Locate_DevNodeW(
                    &dev, const_cast<DEVINSTID_W>(fullId.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
                    CR_SUCCESS &&
                CM_Locate_DevNodeW(
                    &dev, const_cast<DEVINSTID_W>(fullId.c_str()), CM_LOCATE_DEVNODE_PHANTOM) ==
                    CR_SUCCESS)
            {
                if (CM_Uninstall_DevNode(dev, 0) == CR_SUCCESS) {
                    ++removed;
                }
            }
        }
        RegCloseKey(subKey);
    }
    RegCloseKey(bthKey);
    return removed;
}

uint32_t RadioDriverManager::CarryLeDeviceIds()
{
    HKEY leRoot = nullptr;
    HKEY pairedRoot = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Enum\\BTHLEDevice", 0,
            KEY_READ | KEY_WOW64_64KEY, &leRoot) != ERROR_SUCCESS)
    {
        return 0;
    }
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SYSTEM\\CurrentControlSet\\Services\\BTHPORT\\Parameters\\Devices", 0,
            KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, &pairedRoot) != ERROR_SUCCESS)
    {
        RegCloseKey(leRoot);
        return 0;
    }

    static const std::wregex kLeRe{
        L"_Dev_VID&([0-9a-f]{2})([0-9a-f]{4})_PID&([0-9a-f]{4})_REV&([0-9a-f]{4})_([0-9a-f]{12})$",
        std::regex_constants::icase};

    uint32_t carried = 0;
    wchar_t subName[256]{};
    for (DWORD i = 0;; ++i) {
        DWORD len = 256;
        if (RegEnumKeyExW(leRoot, i, subName, &len, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS)
        {
            break;
        }
        std::wstring s{subName, len};
        std::wsmatch m;
        if (!std::regex_search(s, m, kLeRe)) {
            continue;
        }
        HKEY devKey = nullptr;
        if (RegOpenKeyExW(
                pairedRoot, m[5].str().c_str(), 0, KEY_READ | KEY_WRITE | KEY_WOW64_64KEY,
                &devKey) != ERROR_SUCCESS)
        {
            continue;
        }
        DWORD existingVid = 0;
        DWORD existingSize = sizeof(existingVid);
        if (RegQueryValueExW(
                devKey, L"VID", nullptr, nullptr, reinterpret_cast<LPBYTE>(&existingVid),
                &existingSize) != ERROR_SUCCESS)
        {
            const DWORD vidType = std::stoul(m[1].str(), nullptr, 16);
            const DWORD vid = std::stoul(m[2].str(), nullptr, 16);
            const DWORD pid = std::stoul(m[3].str(), nullptr, 16);
            const DWORD rev = std::stoul(m[4].str(), nullptr, 16);
            RegSetValueExW(
                devKey, L"VIDType", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&vidType), 4);
            RegSetValueExW(devKey, L"VID", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&vid), 4);
            RegSetValueExW(devKey, L"PID", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&pid), 4);
            RegSetValueExW(
                devKey, L"Version", 0, REG_DWORD, reinterpret_cast<const BYTE *>(&rev), 4);
            ++carried;
        }
        RegCloseKey(devKey);
    }

    RegCloseKey(pairedRoot);
    RegCloseKey(leRoot);
    return carried;
}

} // namespace Service::Win
