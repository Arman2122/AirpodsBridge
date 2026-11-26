// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/AudioControl.h"

// clang-format off
#include "Source/Support/OS/Windows.h"
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <avrt.h>
#include <cfgmgr32.h>
#include <endpointvolume.h>
#include <Functiondiscoverykeys_devpkey.h>
#include <objbase.h>
#include <oleauto.h>
#include <propvarutil.h>
#include <shellapi.h>
#include <wbemidl.h>
// clang-format on

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMetaObject>

#include "Source/Support/Logger.h"

namespace Core::AudioControl {

namespace {

// Undocumented-but-stable Windows Core Audio COM interface used to change default audio
// endpoints since Windows 7 through Windows 11 Build 28000 (CLSID_CPolicyConfigClient).
// Every call checks HRESULT cleanly and degrades gracefully if unavailable.
MIDL_INTERFACE("f8679f50-850a-41cf-9c72-430f290290c8")
IPolicyConfig : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE GetMixFormat(PCWSTR, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetDeviceFormat(PCWSTR, INT, WAVEFORMATEX **) = 0;
    virtual HRESULT STDMETHODCALLTYPE ResetDeviceFormat(PCWSTR) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDeviceFormat(PCWSTR, WAVEFORMATEX *, WAVEFORMATEX *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetProcessingPeriod(PCWSTR, INT, PINT64, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetProcessingPeriod(PCWSTR, PINT64) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetShareMode(PCWSTR, struct DeviceShareMode *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetShareMode(PCWSTR, struct DeviceShareMode *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetPropertyValue(
        PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetPropertyValue(
        PCWSTR, const PROPERTYKEY &, PROPVARIANT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetDefaultEndpoint(PCWSTR wszDeviceId, ERole eRole) = 0;
    virtual HRESULT STDMETHODCALLTYPE SetEndpointVisibility(PCWSTR, INT) = 0;
};

constexpr CLSID kClsidPolicyConfigClient = {
    0x870af99c, 0x171d, 0x4f9e, {0xaf, 0x0d, 0xe6, 0x3d, 0xf4, 0x0c, 0x2b, 0xc9}};

constexpr wchar_t kBthA2dpParamsKey[] = L"SYSTEM\\CurrentControlSet\\Services\\BthA2dp\\Parameters";
constexpr wchar_t kMmDevicesRenderKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render";
constexpr wchar_t kDisableSysFxValueName[] = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";
constexpr wchar_t kPropDeviceNameValue[] = L"{b3f8fa53-0004-438e-9003-51a46e139bfc},6";
constexpr wchar_t kPropEndpointFriendlyValue[] = L"{a45c254e-df1c-4efd-8020-67d146a850e0},2";
constexpr wchar_t kPropContainerIdValue[] = L"{8c7ed206-3f8a-4827-b3ab-ae9e1faefc6c},2";
constexpr wchar_t kBluetoothClassGuid[] = L"{e0cbf06c-cd8b-4647-bb8a-263b43f0f974}";

constexpr DEVPROPKEY kDevPkeyFriendlyName{
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
constexpr DEVPROPKEY kDevPkeyContainerId{
    {0x8c7ed206, 0x3f8a, 0x4827, {0xb3, 0xab, 0xae, 0x9e, 0x1f, 0xae, 0xfc, 0x6c}}, 2};
constexpr PROPERTYKEY kPkeyAudioEndpointDisableSysFx{
    {0x1da5d803, 0xd492, 0x4edd, {0x8c, 0x23, 0xe0, 0xc0, 0xff, 0xee, 0x7f, 0x0e}}, 5};

QString NormalizeGuid(QString id)
{
    id = id.trimmed();
    if (id.startsWith('{') && id.endsWith('}')) {
        id = id.mid(1, id.size() - 2);
    }
    return id.toLower();
}

// Extracts the `{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}` endpoint GUID from a WASAPI endpoint
// ID such as `{0.0.0.00000000}.{3eb939f5-52b0-4655-ab0e-981f1f51ab83}`.
QString ExtractEndpointGuid(const QString &endpointId)
{
    const int open = endpointId.lastIndexOf('{');
    if (open < 0 || !endpointId.endsWith('}')) {
        return {};
    }
    const QString candidate = endpointId.mid(open);
    if (candidate.size() != 38) {
        return {};
    }
    for (int i = 1; i + 1 < candidate.size(); ++i) {
        const QChar c = candidate.at(i);
        const bool ok =
            (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == '-';
        if (!ok) {
            return {};
        }
    }
    return candidate.toLower();
}

// Reads a DWORD from native 64-bit HKLM (bypassing WOW6432Node from this Win32 binary).
std::optional<DWORD> ReadHklmDword64(const std::wstring &subKey, const wchar_t *valueName)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) !=
        ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD value = 0;
    DWORD type = 0;
    DWORD size = sizeof(value);
    const LONG res =
        RegQueryValueExW(key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(&value), &size);
    RegCloseKey(key);
    if (res != ERROR_SUCCESS || type != REG_DWORD || size != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

// Writes a DWORD to native 64-bit HKLM (bypassing WOW6432Node on x64 and ARM64 Windows;
// safely ignored on native x86). Tries `RegOpenKeyExW` before `RegCreateKeyExW` so existing
// keys where Administrators hold `SetValue` without parent `CreateSubKey` (such as
// `MMDevices\Audio\Render\{guid}\FxProperties`) succeed cleanly.
bool WriteHklmDword64(const std::wstring &subKey, const wchar_t *valueName, DWORD value)
{
    HKEY key = nullptr;
    LONG openRes =
        RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &key);
    if (openRes != ERROR_SUCCESS) {
        openRes = RegCreateKeyExW(
            HKEY_LOCAL_MACHINE, subKey.c_str(), 0, nullptr, REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    }
    if (openRes != ERROR_SUCCESS || key == nullptr) {
        return false;
    }

    const LONG setRes = RegSetValueExW(
        key, valueName, 0, REG_DWORD, reinterpret_cast<const BYTE *>(&value), sizeof(value));
    RegCloseKey(key);
    return setRes == ERROR_SUCCESS;
}

std::optional<QString> ReadHklmString64(const std::wstring &subKey, const wchar_t *valueName)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &key) !=
        ERROR_SUCCESS)
    {
        return std::nullopt;
    }

    DWORD type = 0;
    DWORD byteSize = 0;
    LONG res = RegQueryValueExW(key, valueName, nullptr, &type, nullptr, &byteSize);
    if (res != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ) || byteSize == 0) {
        RegCloseKey(key);
        return std::nullopt;
    }

    std::wstring buf(byteSize / sizeof(wchar_t) + 1, L'\0');
    res = RegQueryValueExW(
        key, valueName, nullptr, &type, reinterpret_cast<LPBYTE>(buf.data()), &byteSize);
    RegCloseKey(key);
    if (res != ERROR_SUCCESS) {
        return std::nullopt;
    }
    buf.resize(wcsnlen(buf.c_str(), buf.size()));
    return QString::fromStdWString(buf);
}

QString ValidateBracedGuid(const QString &raw)
{
    QString trimmed = raw.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }
    if (!trimmed.startsWith('{')) {
        trimmed.prepend('{');
    }
    if (!trimmed.endsWith('}')) {
        trimmed.append('}');
    }
    return ExtractEndpointGuid(trimmed);
}

bool IsAppleAudioDeviceName(const QString &name)
{
    return name.contains(QStringLiteral("AirPods"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("Powerbeats"), Qt::CaseInsensitive) ||
           name.contains(QStringLiteral("Beats"), Qt::CaseInsensitive);
}

std::vector<std::wstring> EnumerateCmDeviceIds(const wchar_t *filter, ULONG flags)
{
    ULONG charCount = 0;
    if (CM_Get_Device_ID_List_SizeW(&charCount, filter, flags) != CR_SUCCESS || charCount <= 1) {
        return {};
    }

    std::vector<wchar_t> multiSz(charCount + 2, L'\0');
    if (CM_Get_Device_ID_ListW(filter, multiSz.data(), charCount, flags) != CR_SUCCESS) {
        return {};
    }

    std::vector<std::wstring> out;
    const wchar_t *cursor = multiSz.data();
    while (*cursor != L'\0') {
        const size_t len = wcslen(cursor);
        out.emplace_back(cursor, len);
        cursor += len + 1;
    }
    return out;
}

QString ReadDevNodeStringProperty(DEVINST devInst, const DEVPROPKEY &propKey)
{
    DEVPROPTYPE propType = DEVPROP_TYPE_EMPTY;
    ULONG byteSize = 0;
    CONFIGRET cr = CM_Get_DevNode_PropertyW(devInst, &propKey, &propType, nullptr, &byteSize, 0);
    if ((cr != CR_BUFFER_SMALL && cr != CR_SUCCESS) || byteSize == 0 ||
        propType != DEVPROP_TYPE_STRING)
    {
        return {};
    }

    std::vector<wchar_t> buf(byteSize / sizeof(wchar_t) + 1, L'\0');
    cr = CM_Get_DevNode_PropertyW(
        devInst, &propKey, &propType, reinterpret_cast<PBYTE>(buf.data()), &byteSize, 0);
    if (cr != CR_SUCCESS) {
        return {};
    }
    return QString::fromWCharArray(buf.data());
}

QString ReadDevNodeGuidProperty(DEVINST devInst, const DEVPROPKEY &propKey)
{
    DEVPROPTYPE propType = DEVPROP_TYPE_EMPTY;
    GUID guid{};
    ULONG byteSize = sizeof(guid);
    if (CM_Get_DevNode_PropertyW(
            devInst, &propKey, &propType, reinterpret_cast<PBYTE>(&guid), &byteSize, 0) !=
            CR_SUCCESS ||
        propType != DEVPROP_TYPE_GUID)
    {
        return {};
    }
    wchar_t buf[39]{};
    if (StringFromGUID2(guid, buf, 39) == 0) {
        return {};
    }
    return QString::fromWCharArray(buf);
}

// Updates USB Bluetooth radio power-saving suspend (`MSPower_DeviceEnable`) via native COM WMI
// (`ROOT\WMI`), completely replacing PowerShell `Get-CimInstance` / `Set-CimInstance`.
void UpdateUsbBluetoothPowerPolicyWmi(bool lowLatencySbc)
{
    const auto btIds = EnumerateCmDeviceIds(
        kBluetoothClassGuid, CM_GETIDLIST_FILTER_CLASS | CM_GETIDLIST_FILTER_PRESENT);
    std::vector<QString> usbRadioPrefixes;
    for (const auto &id : btIds) {
        const QString qid = QString::fromStdWString(id);
        if (qid.startsWith(QStringLiteral("USB\\"), Qt::CaseInsensitive)) {
            usbRadioPrefixes.push_back(qid);
        }
    }
    if (usbRadioPrefixes.empty()) {
        return;
    }

    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(comHr);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) {
        return;
    }

    const HRESULT secHr = CoInitializeSecurity(
        nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE,
        nullptr, EOAC_NONE, nullptr);
    if (FAILED(secHr) && secHr != RPC_E_TOO_LATE) {
        if (needUninit) {
            CoUninitialize();
        }
        return;
    }

    IWbemLocator *locator = nullptr;
    IWbemServices *services = nullptr;
    IEnumWbemClassObject *enumerator = nullptr;

    if (SUCCEEDED(CoCreateInstance(
            CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
            reinterpret_cast<void **>(&locator))) &&
        locator != nullptr)
    {
        BSTR ns = SysAllocString(L"ROOT\\WMI");
        if (ns != nullptr &&
            SUCCEEDED(locator->ConnectServer(
                ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services)) &&
            services != nullptr)
        {
            CoSetProxyBlanket(
                services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

            BSTR wql = SysAllocString(L"WQL");
            BSTR query = SysAllocString(L"SELECT * FROM MSPower_DeviceEnable");
            if (wql != nullptr && query != nullptr &&
                SUCCEEDED(services->ExecQuery(
                    wql, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr,
                    &enumerator)) &&
                enumerator != nullptr)
            {
                IWbemClassObject *obj = nullptr;
                ULONG returned = 0;
                while (enumerator->Next(5000, 1, &obj, &returned) == WBEM_S_NO_ERROR &&
                       returned == 1 && obj != nullptr)
                {
                    VARIANT instVar;
                    VariantInit(&instVar);
                    if (SUCCEEDED(obj->Get(L"InstanceName", 0, &instVar, nullptr, nullptr)) &&
                        instVar.vt == VT_BSTR && instVar.bstrVal != nullptr)
                    {
                        const QString instName = QString::fromWCharArray(instVar.bstrVal);
                        const bool matchesRadio = std::any_of(
                            usbRadioPrefixes.begin(), usbRadioPrefixes.end(),
                            [&](const QString &prefix) {
                                return instName.startsWith(prefix, Qt::CaseInsensitive);
                            });
                        if (matchesRadio) {
                            VARIANT enableVar;
                            VariantInit(&enableVar);
                            enableVar.vt = VT_BOOL;
                            enableVar.boolVal = lowLatencySbc ? VARIANT_FALSE : VARIANT_TRUE;
                            if (SUCCEEDED(obj->Put(L"Enable", 0, &enableVar, 0))) {
                                services->PutInstance(obj, WBEM_FLAG_UPDATE_ONLY, nullptr, nullptr);
                            }
                            VariantClear(&enableVar);
                        }
                    }
                    VariantClear(&instVar);
                    obj->Release();
                    obj = nullptr;
                }
                enumerator->Release();
            }
            SysFreeString(wql);
            SysFreeString(query);
            services->Release();
        }
        SysFreeString(ns);
        locator->Release();
    }

    if (needUninit) {
        CoUninitialize();
    }
}

// Disables or restores Windows USB Bluetooth Selective Suspend registry keys so the radio
// never parks in USB D2/D3 power-saving states during real-time audio streaming.
void UpdateUsbBluetoothSelectiveSuspendRegistry(bool lowLatencySbc)
{
    const auto btIds = EnumerateCmDeviceIds(
        kBluetoothClassGuid, CM_GETIDLIST_FILTER_CLASS | CM_GETIDLIST_FILTER_PRESENT);
    const DWORD suspendVal = lowLatencySbc ? 0u : 1u;
    for (const auto &id : btIds) {
        const QString qid = QString::fromStdWString(id);
        if (!qid.startsWith(QStringLiteral("USB\\"), Qt::CaseInsensitive)) {
            continue;
        }
        const std::wstring paramsKey =
            L"SYSTEM\\CurrentControlSet\\Enum\\" + id + L"\\Device Parameters";
        WriteHklmDword64(paramsKey, L"SelectiveSuspendEnabled", suspendVal);
        WriteHklmDword64(paramsKey, L"EnhancedPowerManagementEnabled", suspendVal);
        WriteHklmDword64(paramsKey, L"AllowIdleIrpInD3", suspendVal);
    }
}

// Re-negotiates present AirPods / Beats A2DP Sink PnP nodes (`0000110B`) via `cfgmgr32.dll`
// so the updated `BthA2dp` codec and `Disable_SysFx` APO settings apply immediately without
// rebooting Windows. Works natively on x86, x64, and ARM64 Windows without PowerShell.
bool RenegotiateAppleA2dpNodes(const QString &normalizedContainerId)
{
    const auto allPresent = EnumerateCmDeviceIds(nullptr, CM_GETIDLIST_FILTER_PRESENT);
    std::vector<DEVINST> targets;
    for (const auto &id : allPresent) {
        const QString qid = QString::fromStdWString(id);
        if (!qid.contains(QStringLiteral("0000110B"), Qt::CaseInsensitive)) {
            continue;
        }
        DEVINST devInst = 0;
        if (CM_Locate_DevNodeW(
                &devInst, const_cast<DEVINSTID_W>(id.c_str()), CM_LOCATE_DEVNODE_NORMAL) !=
            CR_SUCCESS)
        {
            continue;
        }

        bool matched = false;
        if (!normalizedContainerId.isEmpty()) {
            const QString nodeContainer =
                NormalizeGuid(ReadDevNodeGuidProperty(devInst, kDevPkeyContainerId));
            if (nodeContainer == normalizedContainerId) {
                matched = true;
            }
        }
        if (!matched) {
            const QString friendly = ReadDevNodeStringProperty(devInst, kDevPkeyFriendlyName);
            if (IsAppleAudioDeviceName(friendly)) {
                matched = true;
            }
        }
        if (matched) {
            targets.push_back(devInst);
        }
    }

    bool restartedAny = false;
    for (const DEVINST devInst : targets) {
        const CONFIGRET dis = CM_Disable_DevNode(devInst, CM_DISABLE_UI_NOT_OK);
        if (dis == CR_SUCCESS) {
            Sleep(600);
            const CONFIGRET en = CM_Enable_DevNode(devInst, 0);
            if (en == CR_SUCCESS) {
                restartedAny = true;
            }
        }
    }
    return restartedAny;
}

QString GuidToString(const GUID &guid)
{
    wchar_t buffer[39]{};
    if (StringFromGUID2(guid, buffer, 39) == 0) {
        return {};
    }
    return QString::fromWCharArray(buffer);
}

QString ReadStringProp(IPropertyStore *store, const PROPERTYKEY &key)
{
    if (store == nullptr) {
        return {};
    }
    PROPVARIANT var;
    PropVariantInit(&var);
    QString out;
    if (SUCCEEDED(store->GetValue(key, &var))) {
        if (var.vt == VT_LPWSTR && var.pwszVal != nullptr) {
            out = QString::fromWCharArray(var.pwszVal);
        }
        else if (var.vt == VT_CLSID && var.puuid != nullptr) {
            out = GuidToString(*var.puuid);
        }
    }
    PropVariantClear(&var);
    return out;
}

QString ResolveProcessName(DWORD pid)
{
    if (pid == 0) {
        return {};
    }
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc == nullptr) {
        return {};
    }
    wchar_t path[MAX_PATH * 2]{};
    DWORD size = static_cast<DWORD>(std::size(path));
    QString result;
    if (QueryFullProcessImageNameW(proc, 0, path, &size) != FALSE && size > 0) {
        result = QFileInfo{QString::fromWCharArray(path, static_cast<int>(size))}.fileName();
    }
    CloseHandle(proc);
    return result;
}

int EndpointStateRank(DWORD state)
{
    if (state == DEVICE_STATE_ACTIVE) {
        return 4;
    }
    if (state == DEVICE_STATE_UNPLUGGED) {
        return 3;
    }
    if (state == DEVICE_STATE_DISABLED) {
        return 2;
    }
    return 1; // DEVICE_STATE_NOTPRESENT
}

bool LooksLikeHandsFree(const QString &friendlyName, const QString &ifaceDesc)
{
    return friendlyName.contains(QStringLiteral("Hands-Free"), Qt::CaseInsensitive) ||
           ifaceDesc.contains(QStringLiteral("Hands-Free"), Qt::CaseInsensitive) ||
           friendlyName.startsWith(QStringLiteral("Headset"), Qt::CaseInsensitive);
}

// Updates `PKEY_AudioEndpoint_Disable_SysFx` via Windows' own `IPolicyConfig::SetPropertyValue`
// (the same COM API `mmsys.cpl` uses), which routes through `AudioEndpointBuilder` (`SYSTEM`)
// so APO bypass takes effect live and persists across reconnects without ACL errors.
bool SetAudioEnhancementsViaPolicyConfig(
    bool lowLatencySbc, const QString &validatedGuid, const QString &normalizedContainer)
{
    const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool needUninit = SUCCEEDED(comHr);
    if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE) {
        return false;
    }

    bool updatedAny = false;
    Support::OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> enumerator;
    IPolicyConfig *policy = nullptr;

    if (SUCCEEDED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, enumerator.GetIID(),
            reinterpret_cast<void **>(enumerator.ReleaseAndAddressOf()))) &&
        enumerator &&
        SUCCEEDED(CoCreateInstance(
            kClsidPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig),
            reinterpret_cast<void **>(&policy))) &&
        policy != nullptr)
    {
        Support::OS::Windows::Com::UniquePtr<IMMDeviceCollection> collection;
        if (SUCCEEDED(enumerator->EnumAudioEndpoints(
                eRender, DEVICE_STATEMASK_ALL, collection.ReleaseAndAddressOf())) &&
            collection)
        {
            UINT count = 0;
            if (SUCCEEDED(collection->GetCount(&count))) {
                for (UINT i = 0; i < count; ++i) {
                    Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
                    if (FAILED(collection->Item(i, dev.ReleaseAndAddressOf())) || !dev) {
                        continue;
                    }
                    LPWSTR rawId = nullptr;
                    if (FAILED(dev->GetId(&rawId)) || rawId == nullptr) {
                        continue;
                    }
                    const QString epId = QString::fromWCharArray(rawId);
                    CoTaskMemFree(rawId);

                    const QString epGuid = ExtractEndpointGuid(epId);
                    bool matched = !validatedGuid.isEmpty() &&
                                   epGuid.compare(validatedGuid, Qt::CaseInsensitive) == 0;

                    if (!matched) {
                        Support::OS::Windows::Com::UniquePtr<IPropertyStore> store;
                        if (SUCCEEDED(
                                dev->OpenPropertyStore(STGM_READ, store.ReleaseAndAddressOf())) &&
                            store)
                        {
                            const QString friendly =
                                ReadStringProp(store.operator->(), PKEY_Device_FriendlyName);
                            const QString ifaceDesc = ReadStringProp(
                                store.operator->(), PKEY_DeviceInterface_FriendlyName);
                            const QString epContainer = NormalizeGuid(
                                ReadStringProp(store.operator->(), PKEY_Device_ContainerId));

                            if (!LooksLikeHandsFree(friendly, ifaceDesc)) {
                                if (!normalizedContainer.isEmpty() &&
                                    epContainer == normalizedContainer)
                                {
                                    matched = true;
                                }
                                else if (
                                    IsAppleAudioDeviceName(friendly) ||
                                    IsAppleAudioDeviceName(ifaceDesc))
                                {
                                    matched = true;
                                }
                            }
                        }
                    }

                    if (!matched) {
                        continue;
                    }

                    PROPVARIANT pv;
                    PropVariantInit(&pv);
                    pv.vt = VT_UI4;
                    pv.ulVal = lowLatencySbc ? 1u : 0u;
                    const std::wstring wideEpId = epId.toStdWString();
                    if (SUCCEEDED(policy->SetPropertyValue(
                            wideEpId.c_str(), kPkeyAudioEndpointDisableSysFx, &pv)))
                    {
                        updatedAny = true;
                    }
                    PropVariantClear(&pv);
                }
            }
        }
    }

    if (policy != nullptr) {
        policy->Release();
    }
    enumerator = nullptr;
    if (needUninit) {
        CoUninitialize();
    }
    return updatedAny;
}

bool SetDefaultAudioEndpoint(const QString &endpointId, bool includeCommunications)
{
    if (endpointId.isEmpty()) {
        return false;
    }

    IPolicyConfig *policy = nullptr;
    HRESULT hr = CoCreateInstance(
        kClsidPolicyConfigClient, nullptr, CLSCTX_ALL, __uuidof(IPolicyConfig),
        reinterpret_cast<void **>(&policy));
    if (FAILED(hr) || policy == nullptr) {
        LOG(Warn, "AudioControl: IPolicyConfig unavailable (hr={:#x}).", static_cast<uint32_t>(hr));
        return false;
    }

    const std::wstring wideId = endpointId.toStdWString();
    hr = policy->SetDefaultEndpoint(wideId.c_str(), eConsole);
    if (SUCCEEDED(hr)) {
        policy->SetDefaultEndpoint(wideId.c_str(), eMultimedia);
        if (includeCommunications) {
            policy->SetDefaultEndpoint(wideId.c_str(), eCommunications);
        }
    }
    else {
        LOG(Warn, "AudioControl: SetDefaultEndpoint failed (hr={:#x}).", static_cast<uint32_t>(hr));
    }

    policy->Release();
    return SUCCEEDED(hr);
}

QString GetDefaultEndpointId(IMMDeviceEnumerator *enumerator, EDataFlow flow, ERole role)
{
    if (enumerator == nullptr) {
        return {};
    }
    Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
    if (FAILED(enumerator->GetDefaultAudioEndpoint(flow, role, dev.ReleaseAndAddressOf())) || !dev)
    {
        return {};
    }
    LPWSTR rawId = nullptr;
    if (FAILED(dev->GetId(&rawId)) || rawId == nullptr) {
        return {};
    }
    QString id = QString::fromWCharArray(rawId);
    CoTaskMemFree(rawId);
    return id;
}

std::vector<AudioEndpoint> EnumerateEndpointsOnCurrentThread(EDataFlow flow)
{
    std::vector<AudioEndpoint> list;
    Support::OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(
            __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, enumerator.GetIID(),
            reinterpret_cast<void **>(enumerator.ReleaseAndAddressOf()))) ||
        !enumerator)
    {
        return list;
    }

    const QString defaultId = GetDefaultEndpointId(enumerator.operator->(), flow, eConsole);

    Support::OS::Windows::Com::UniquePtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(
            flow, DEVICE_STATE_ACTIVE, collection.ReleaseAndAddressOf())) ||
        !collection)
    {
        return list;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        return list;
    }

    list.reserve(count);
    for (UINT i = 0; i < count; ++i) {
        Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
        if (FAILED(collection->Item(i, dev.ReleaseAndAddressOf())) || !dev) {
            continue;
        }
        LPWSTR rawId = nullptr;
        if (FAILED(dev->GetId(&rawId)) || rawId == nullptr) {
            continue;
        }
        QString id = QString::fromWCharArray(rawId);
        CoTaskMemFree(rawId);

        Support::OS::Windows::Com::UniquePtr<IPropertyStore> store;
        QString friendlyName;
        QString ifaceDesc;
        if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, store.ReleaseAndAddressOf())) && store) {
            friendlyName = ReadStringProp(store.operator->(), PKEY_Device_FriendlyName);
            ifaceDesc = ReadStringProp(store.operator->(), PKEY_DeviceInterface_FriendlyName);
        }
        if (friendlyName.isEmpty()) {
            friendlyName = id;
        }

        list.push_back(AudioEndpoint{
            .id = id,
            .name = friendlyName,
            .isDefault = (id == defaultId),
            .isBluetoothHfp = LooksLikeHandsFree(friendlyName, ifaceDesc),
        });
    }

    std::sort(list.begin(), list.end(), [](const AudioEndpoint &a, const AudioEndpoint &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    return list;
}

} // namespace

//
// Dedicated MTA worker + COM notification bridge + optional wear-aware event-driven WASAPI
// silent keep-alive thread. All COM interfaces live and die strictly on their own MTA threads,
// keeping Qt's GUI STA thread untouched and free of COM stalls.
//
class Service::Impl final : public IMMNotificationClient,
                            public IAudioSessionNotification,
                            public IAudioSessionEvents
{
public:
    explicit Impl(Service *owner) : _owner{owner}
    {
        _shutdownEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        _worker = std::thread{&Impl::WorkerLoop, this};
    }

    ~Impl()
    {
        Shutdown();
    }

    void Shutdown()
    {
        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            if (_stopping) {
                return;
            }
            _stopping = true;
            _tasks.clear();
        }
        if (_shutdownEvent != nullptr) {
            SetEvent(_shutdownEvent);
        }
        _cv.notify_all();
        if (_worker.joinable()) {
            _worker.join();
        }
        if (_shutdownEvent != nullptr) {
            CloseHandle(_shutdownEvent);
            _shutdownEvent = nullptr;
        }
    }

    void Post(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            if (_stopping) {
                return;
            }
            _tasks.push_back(std::move(task));
        }
        _cv.notify_one();
    }

    void SetTrackedContainerId(const QString &containerId)
    {
        const QString normalized = NormalizeGuid(containerId);
        Post([this, normalized] {
            if (_trackedContainerId == normalized) {
                return;
            }
            _trackedContainerId = normalized;
            RebindTrackedEndpoints();
            RememberCurrentNonAirPodsOutput();
        });
    }

    void SetPreferredMicrophoneId(const QString &endpointId)
    {
        Post([this, endpointId] {
            _preferredMicId = endpointId.trimmed();
            if (!_preferredMicId.isEmpty()) {
                EnforcePreferredMicInternal();
            }
        });
    }

    void SetVolumeCap(bool enabled, uint32_t maxPercent)
    {
        const uint32_t clamped = std::clamp<uint32_t>(maxPercent, 10, 100);
        Post([this, enabled, clamped] {
            _volumeCapEnabled = enabled;
            _volumeCapPercent = clamped;
            if (_volumeCapEnabled) {
                ApplyVolumeCapInternal();
            }
        });
    }

    void SetKeepAliveEnabled(bool enabled)
    {
        Post([this, enabled] {
            if (_keepAliveEnabled == enabled) {
                return;
            }
            _keepAliveEnabled = enabled;
            UpdateKeepAliveState();
            RefreshLatencyDiagnosticsInternal();
        });
    }

    void SetAirPodsWorn(bool worn)
    {
        Post([this, worn] {
            if (_airPodsWorn == worn) {
                return;
            }
            _airPodsWorn = worn;
            UpdateKeepAliveState();
        });
    }

    void SetAutoSwitchOnConnect(bool enabled)
    {
        Post([this, enabled] { _autoSwitchOnConnect = enabled; });
    }

    void RequestSwitchOutputToAirPods()
    {
        Post([this] {
            RememberCurrentNonAirPodsOutput();
            if (_stereoActive && !_airPodsStereoRenderId.isEmpty()) {
                SwitchToAirPodsStereoInternal();
            }
            else {
                _pendingSwitchToAirPods = true;
                _pendingSwitchDeadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds{15};
            }
        });
    }

    void SuspendKeepAliveTemporarily()
    {
        Post([this] {
            if (_keepAliveThread.joinable() || _keepAliveActive.load()) {
                LOG(Info, "AudioControl: draining WASAPI keep-alive stream prior to reconnect.");
                StopKeepAlive();
                RefreshLatencyDiagnosticsInternal();
            }
        });
    }

    void RefreshLatencyDiagnostics()
    {
        Post([this] { RefreshLatencyDiagnosticsInternal(); });
    }

    bool ApplyLatencyProfileElevated(bool lowLatencySbc)
    {
        if (_latencyApplyInFlight.exchange(true)) {
            LOG(Info,
                "AudioControl: latency profile switch already in progress; ignoring duplicate "
                "request.");
            return false;
        }

        const auto diag = GetLatencyDiagnostics();
        const QString validatedGuid = ExtractEndpointGuid(diag.stereoEndpointGuid);
        const QString validatedContainer = ValidateBracedGuid(diag.containerId);

        wchar_t exeBuf[MAX_PATH * 4]{};
        const DWORD exeLen =
            GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)));
        if (exeLen == 0 || exeLen >= std::size(exeBuf)) {
            _latencyApplyInFlight.store(false);
            LOG(Warn, "AudioControl: failed to resolve current executable path for elevation.");
            Post([this] { RefreshLatencyDiagnosticsInternal(true); });
            return false;
        }

        // Launch our own native binary elevated (UAC `runas`, SW_HIDE) with zero PowerShell,
        // zero cmd.exe, zero Sysnative path bugs, and identical behavior on x86, x64, and ARM64.
        QString params = QStringLiteral("--apply-latency-profile %1")
                             .arg(lowLatencySbc ? QStringLiteral("sbc") : QStringLiteral("aac"));
        if (!validatedGuid.isEmpty()) {
            params += QStringLiteral(" --endpoint-guid \"%1\"").arg(validatedGuid);
        }
        if (!validatedContainer.isEmpty()) {
            params += QStringLiteral(" --container-id \"%1\"").arg(validatedContainer);
        }
        const std::wstring wideParams = params.toStdWString();

        SHELLEXECUTEINFOW sei{};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NO_CONSOLE;
        sei.lpVerb = L"runas";
        sei.lpFile = exeBuf;
        sei.lpParameters = wideParams.c_str();
        sei.nShow = SW_HIDE;

        if (ShellExecuteExW(&sei) == FALSE) {
            const DWORD err = GetLastError();
            _latencyApplyInFlight.store(false);
            if (err != ERROR_CANCELLED) {
                LOG(Warn, "AudioControl: elevated native latency switcher launch failed (err={}).",
                    err);
            }
            Post([this] { RefreshLatencyDiagnosticsInternal(true); });
            return false;
        }

        LOG(Info, "AudioControl: launched native elevated latency profile switcher ({}).",
            lowLatencySbc ? "LowLatency/SBC" : "MusicQuality/AAC");
        Post([this] { RefreshLatencyDiagnosticsInternal(true); });

        if (sei.hProcess != nullptr) {
            HANDLE procHandle = sei.hProcess;
            Post([this, procHandle, lowLatencySbc] {
                if (_shutdownEvent != nullptr) {
                    HANDLE waits[2] = {_shutdownEvent, procHandle};
                    WaitForMultipleObjects(2, waits, FALSE, 20000);
                }
                else {
                    WaitForSingleObject(procHandle, 20000);
                }
                DWORD exitCode = 1;
                GetExitCodeProcess(procHandle, &exitCode);
                CloseHandle(procHandle);
                _latencyApplyInFlight.store(false);
                if (!_stopping) {
                    const bool ok = (exitCode == 0 || exitCode == 2);
                    const bool pnpRestarted = (exitCode == 0);
                    LOG(Info,
                        "AudioControl: native latency switcher finished (exit={}, "
                        "pnpRestarted={}).",
                        exitCode, pnpRestarted);
                    RebindTrackedEndpoints();
                    RefreshLatencyDiagnosticsInternal(true);
                    if (ok) {
                        QMetaObject::invokeMethod(
                            _owner,
                            [owner = _owner, lowLatencySbc, pnpRestarted] {
                                emit owner->LatencyProfileApplied(lowLatencySbc, pnpRestarted);
                            },
                            Qt::QueuedConnection);
                    }
                }
            });
        }
        else {
            _latencyApplyInFlight.store(false);
            Post([this] { RefreshLatencyDiagnosticsInternal(true); });
        }
        return true;
    }

    bool SwitchOutputToAirPods()
    {
        std::atomic<bool> result{false};
        std::mutex doneMutex;
        std::condition_variable doneCv;
        bool done = false;

        Post([&] {
            RememberCurrentNonAirPodsOutput();
            result.store(SwitchToAirPodsStereoInternal());
            {
                std::lock_guard<std::mutex> lk{doneMutex};
                done = true;
            }
            doneCv.notify_one();
        });

        std::unique_lock<std::mutex> lk{doneMutex};
        doneCv.wait_for(lk, std::chrono::seconds{2}, [&] { return done; });
        return result.load();
    }

    bool RestorePreviousOutput()
    {
        std::atomic<bool> result{false};
        std::mutex doneMutex;
        std::condition_variable doneCv;
        bool done = false;

        Post([&] {
            result.store(RestorePreviousOutputInternal());
            {
                std::lock_guard<std::mutex> lk{doneMutex};
                done = true;
            }
            doneCv.notify_one();
        });

        std::unique_lock<std::mutex> lk{doneMutex};
        doneCv.wait_for(lk, std::chrono::seconds{2}, [&] { return done; });
        return result.load();
    }

    bool EnforcePreferredMicrophone()
    {
        std::atomic<bool> result{false};
        std::mutex doneMutex;
        std::condition_variable doneCv;
        bool done = false;

        Post([&] {
            result.store(EnforcePreferredMicInternal());
            {
                std::lock_guard<std::mutex> lk{doneMutex};
                done = true;
            }
            doneCv.notify_one();
        });

        std::unique_lock<std::mutex> lk{doneMutex};
        doneCv.wait_for(lk, std::chrono::seconds{2}, [&] { return done; });
        return result.load();
    }

    void ApplyVolumeCapNow()
    {
        Post([this] { ApplyVolumeCapInternal(); });
    }

    void OnConversationAwarenessLevel(uint8_t level)
    {
        Post([this, level] { HandleConversationAwarenessLevelInternal(level); });
    }

    void RestoreConversationDucking()
    {
        Post([this] { RestoreConversationDuckingInternal(); });
    }

    std::vector<AudioEndpoint> ListEndpoints(EDataFlow flow) const
    {
        std::vector<AudioEndpoint> out;
        std::thread{[flow, &out] {
            const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            out = EnumerateEndpointsOnCurrentThread(flow);
            if (SUCCEEDED(hr)) {
                CoUninitialize();
            }
        }}.join();
        return out;
    }

    Core::AirPods::AudioProfile CurrentProfile() const
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        return _profile;
    }

    QString CurrentHfpProcess() const
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        return _hfpProcess;
    }

    LatencyDiagnostics GetLatencyDiagnostics() const
    {
        std::lock_guard<std::mutex> lock{_stateMutex};
        return _latencyDiagnostics;
    }

    // IUnknown (lifetime managed by `Impl` on the worker thread; refcount kept valid for COM)
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++_refCount;
    }
    ULONG STDMETHODCALLTYPE Release() override
    {
        return --_refCount;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **ppv) override
    {
        if (ppv == nullptr) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IMMNotificationClient)) {
            *ppv = static_cast<IMMNotificationClient *>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAudioSessionNotification)) {
            *ppv = static_cast<IAudioSessionNotification *>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAudioSessionEvents)) {
            *ppv = static_cast<IAudioSessionEvents *>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    // IMMNotificationClient (called on MMDevice RPC thread -- NEVER call COM back inline!)
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override
    {
        Post([this] {
            RebindTrackedEndpoints();
            EmitEndpointsChanged();
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override
    {
        Post([this] {
            RebindTrackedEndpoints();
            EmitEndpointsChanged();
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override
    {
        Post([this] {
            RebindTrackedEndpoints();
            EmitEndpointsChanged();
        });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE
    OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR defaultDeviceId) override
    {
        if (flow == eCapture) {
            Post([this] { EnforcePreferredMicInternal(); });
        }
        else if (flow == eRender && role == eConsole && defaultDeviceId != nullptr) {
            QString newId = QString::fromWCharArray(defaultDeviceId);
            Post([this, newId = std::move(newId)] {
                if (!newId.isEmpty() && !IsAirPodsEndpointId(newId)) {
                    _previousOutputId = newId;
                }
            });
        }
        Post([this] { EmitEndpointsChanged(); });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override
    {
        Post([this] { RefreshLatencyDiagnosticsInternal(); });
        return S_OK;
    }

    // IAudioSessionNotification
    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl *) override
    {
        Post([this] { RefreshHfpSessionState(); });
        return S_OK;
    }

    // IAudioSessionEvents
    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float, BOOL, LPCGUID) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD, float[], DWORD, LPCGUID) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID, LPCGUID) override
    {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override
    {
        Post([this] { EvaluateActiveHfpSessions(); });
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(AudioSessionDisconnectReason) override
    {
        Post([this] { RefreshHfpSessionState(); });
        return S_OK;
    }

private:
    void WorkerLoop()
    {
        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        if (SUCCEEDED(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, _enumerator.GetIID(),
                reinterpret_cast<void **>(_enumerator.ReleaseAndAddressOf()))) &&
            _enumerator)
        {
            if (SUCCEEDED(_enumerator->RegisterEndpointNotificationCallback(this))) {
                _registeredDeviceEvents = true;
            }
            RememberCurrentNonAirPodsOutput();
        }

        RefreshLatencyDiagnosticsInternal();

        for (;;) {
            std::function<void()> task;
            bool duckQuietExpired = false;
            {
                std::unique_lock<std::mutex> lock{_queueMutex};
                if (_duckQuietDeadline.has_value()) {
                    _cv.wait_until(lock, _duckQuietDeadline.value(), [this] {
                        return _stopping || !_tasks.empty();
                    });
                }
                else {
                    _cv.wait(lock, [this] { return _stopping || !_tasks.empty(); });
                }
                if (_stopping) {
                    break;
                }
                if (!_tasks.empty()) {
                    task = std::move(_tasks.front());
                    _tasks.pop_front();
                }
                else if (
                    _duckQuietDeadline.has_value() &&
                    std::chrono::steady_clock::now() >= _duckQuietDeadline.value())
                {
                    _duckQuietDeadline.reset();
                    duckQuietExpired = true;
                }
            }
            if (task) {
                task();
            }
            else if (duckQuietExpired) {
                if (_duckActive && _duckLevel > 2) {
                    LOG(Info,
                        "AudioControl: Conversation Awareness quiet timeout after level {}; "
                        "restoring volume.",
                        _duckLevel);
                    RestoreConversationDuckingInternal();
                }
            }
        }

        RestoreConversationDuckingInternal();
        StopKeepAlive();
        DetachSessionWatchers();
        if (_registeredDeviceEvents && _enumerator) {
            _enumerator->UnregisterEndpointNotificationCallback(this);
            _registeredDeviceEvents = false;
        }
        _enumerator = nullptr;

        if (SUCCEEDED(comHr)) {
            CoUninitialize();
        }
    }

    void StopKeepAlive()
    {
        if (_keepAliveStopEvent != nullptr) {
            SetEvent(_keepAliveStopEvent);
        }
        if (_keepAliveThread.joinable()) {
            _keepAliveThread.join();
        }
        if (_keepAliveStopEvent != nullptr) {
            CloseHandle(_keepAliveStopEvent);
            _keepAliveStopEvent = nullptr;
        }
        _keepAliveEndpointId.clear();
        _keepAliveActive.store(false);
    }

    void UpdateKeepAliveState()
    {
        const bool shouldRun = !_stopping && _keepAliveEnabled && _airPodsWorn && _stereoActive &&
                               !_airPodsStereoRenderId.isEmpty();

        if (!shouldRun) {
            if (_keepAliveThread.joinable() || _keepAliveActive.load()) {
                LOG(Info, "AudioControl: stopping wear-aware A2DP keep-alive stream.");
                StopKeepAlive();
                RefreshLatencyDiagnosticsInternal();
            }
            return;
        }

        // If already running on the exact same endpoint and still alive, keep going.
        if (_keepAliveThread.joinable() && _keepAliveActive.load() &&
            _keepAliveEndpointId == _airPodsStereoRenderId)
        {
            return;
        }

        StopKeepAlive();

        _keepAliveStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (_keepAliveStopEvent == nullptr) {
            return;
        }

        _keepAliveEndpointId = _airPodsStereoRenderId;
        const QString targetEndpoint = _airPodsStereoRenderId;
        HANDLE stopEvent = _keepAliveStopEvent;
        _keepAliveThread = std::thread{&Impl::KeepAliveLoop, this, targetEndpoint, stopEvent};
    }

    void KeepAliveLoop(QString endpointId, HANDLE stopEvent)
    {
        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        DWORD mmcssTaskIdx = 0;
        HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &mmcssTaskIdx);

        HANDLE bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        Support::OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> enumerator;
        Support::OS::Windows::Com::UniquePtr<IMMDevice> device;
        Support::OS::Windows::Com::UniquePtr<IAudioClient3> audioClient3;
        Support::OS::Windows::Com::UniquePtr<IAudioClient> audioClient;
        Support::OS::Windows::Com::UniquePtr<IAudioRenderClient> renderClient;
        WAVEFORMATEX *mixFormat = nullptr;

        bool started = false;
        if (bufferEvent != nullptr &&
            SUCCEEDED(CoCreateInstance(
                __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, enumerator.GetIID(),
                reinterpret_cast<void **>(enumerator.ReleaseAndAddressOf()))) &&
            enumerator)
        {
            const std::wstring wideId = endpointId.toStdWString();
            if (SUCCEEDED(enumerator->GetDevice(wideId.c_str(), device.ReleaseAndAddressOf())) &&
                device)
            {
                bool initialized = false;
                bool lowLatencyEngineActive = false;
                double negotiatedPeriodMs = 0.0;

                // Prefer `IAudioClient3::InitializeSharedAudioStream` at the endpoint's minimum
                // hardware engine period (typically 3.0 ms on AirPods vs Windows' 10.0 ms default).
                // Holding an active `IAudioClient3` stream at `minFrames` drops the entire shared
                // WASAPI mixing engine period for EVERY game, browser, and app on this endpoint.
                if (SUCCEEDED(device->Activate(
                        __uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void **>(audioClient3.ReleaseAndAddressOf()))) &&
                    audioClient3 && SUCCEEDED(audioClient3->GetMixFormat(&mixFormat)) &&
                    mixFormat != nullptr)
                {
                    UINT32 defaultFrames = 0;
                    UINT32 fundamentalFrames = 0;
                    UINT32 minFrames = 0;
                    UINT32 maxFrames = 0;
                    if (SUCCEEDED(audioClient3->GetSharedModeEnginePeriod(
                            mixFormat, &defaultFrames, &fundamentalFrames, &minFrames,
                            &maxFrames)) &&
                        minFrames > 0)
                    {
                        const HRESULT llHr = audioClient3->InitializeSharedAudioStream(
                            AUDCLNT_STREAMFLAGS_EVENTCALLBACK, minFrames, mixFormat, nullptr);
                        if (SUCCEEDED(llHr)) {
                            audioClient3->QueryInterface(
                                __uuidof(IAudioClient),
                                reinterpret_cast<void **>(audioClient.ReleaseAndAddressOf()));
                            initialized = static_cast<bool>(audioClient);
                            lowLatencyEngineActive = initialized;
                            if (mixFormat->nSamplesPerSec > 0) {
                                negotiatedPeriodMs = (static_cast<double>(minFrames) * 1000.0) /
                                                     static_cast<double>(mixFormat->nSamplesPerSec);
                            }
                        }
                    }

                    if (!initialized) {
                        // Release the failed `IAudioClient3` instance before activating a clean
                        // legacy `IAudioClient` fallback so `Initialize` never sees stale state.
                        audioClient3 = nullptr;
                        CoTaskMemFree(mixFormat);
                        mixFormat = nullptr;
                    }
                }

                if (!initialized &&
                    SUCCEEDED(device->Activate(
                        __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void **>(audioClient.ReleaseAndAddressOf()))) &&
                    audioClient)
                {
                    REFERENCE_TIME defaultPeriod = 0;
                    REFERENCE_TIME minPeriod = 0;
                    if (SUCCEEDED(audioClient->GetMixFormat(&mixFormat)) && mixFormat != nullptr &&
                        SUCCEEDED(audioClient->GetDevicePeriod(&defaultPeriod, &minPeriod)) &&
                        SUCCEEDED(audioClient->Initialize(
                            AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                            defaultPeriod, 0, mixFormat, nullptr)))
                    {
                        initialized = true;
                        negotiatedPeriodMs = static_cast<double>(defaultPeriod) / 10000.0;
                    }
                }

                UINT32 bufferFrames = 0;
                if (initialized && audioClient &&
                    SUCCEEDED(audioClient->SetEventHandle(bufferEvent)) &&
                    SUCCEEDED(audioClient->GetBufferSize(&bufferFrames)) && bufferFrames > 0 &&
                    SUCCEEDED(audioClient->GetService(
                        __uuidof(IAudioRenderClient),
                        reinterpret_cast<void **>(renderClient.ReleaseAndAddressOf()))) &&
                    renderClient)
                {
                    BYTE *data = nullptr;
                    if (SUCCEEDED(renderClient->GetBuffer(bufferFrames, &data))) {
                        renderClient->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
                    }

                    if (SUCCEEDED(audioClient->Start())) {
                        started = true;
                        _keepAliveActive.store(true);
                        if (lowLatencyEngineActive) {
                            LOG(Info,
                                "AudioControl: wear-aware WASAPI IAudioClient3 low-latency engine "
                                "active ({} Hz, {} frames, {:.1f} ms period).",
                                mixFormat->nSamplesPerSec, bufferFrames, negotiatedPeriodMs);
                        }
                        else {
                            LOG(Info,
                                "AudioControl: wear-aware WASAPI A2DP keep-alive active ({} Hz, {} "
                                "frames, {:.1f} ms period).",
                                mixFormat->nSamplesPerSec, bufferFrames, negotiatedPeriodMs);
                        }
                        Post([this] { RefreshLatencyDiagnosticsInternal(true); });

                        HANDLE waits[2] = {stopEvent, bufferEvent};
                        while (true) {
                            const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, 500);
                            if (wait == WAIT_OBJECT_0) {
                                break;
                            }
                            if (wait == WAIT_TIMEOUT) {
                                continue;
                            }
                            if (wait != WAIT_OBJECT_0 + 1) {
                                break;
                            }

                            UINT32 padding = 0;
                            const HRESULT padHr = audioClient->GetCurrentPadding(&padding);
                            if (FAILED(padHr)) {
                                break;
                            }
                            if (padding >= bufferFrames) {
                                continue;
                            }
                            const UINT32 available = bufferFrames - padding;
                            BYTE *buf = nullptr;
                            const HRESULT bufHr = renderClient->GetBuffer(available, &buf);
                            if (FAILED(bufHr)) {
                                break;
                            }
                            const HRESULT relHr =
                                renderClient->ReleaseBuffer(available, AUDCLNT_BUFFERFLAGS_SILENT);
                            if (FAILED(relHr)) {
                                break;
                            }
                        }

                        audioClient->Stop();
                    }
                }
            }
        }

        if (mixFormat != nullptr) {
            CoTaskMemFree(mixFormat);
        }
        renderClient = nullptr;
        audioClient = nullptr;
        audioClient3 = nullptr;
        device = nullptr;
        enumerator = nullptr;
        if (bufferEvent != nullptr) {
            CloseHandle(bufferEvent);
        }
        if (mmcssHandle != nullptr) {
            AvRevertMmThreadCharacteristics(mmcssHandle);
        }
        if (SUCCEEDED(comHr)) {
            CoUninitialize();
        }

        const bool wasActive = _keepAliveActive.exchange(false);
        if (started || wasActive) {
            Post([this] { RefreshLatencyDiagnosticsInternal(); });
        }
    }

    void RefreshLatencyDiagnosticsInternal(bool forceEmit = false)
    {
        LatencyDiagnostics diag;
        const auto aacDword = ReadHklmDword64(kBthA2dpParamsKey, L"BluetoothAacEnable");
        diag.aacEnabled = !aacDword.has_value() || aacDword.value() != 0;
        diag.codec = diag.aacEnabled ? QStringLiteral("AAC") : QStringLiteral("SBC");
        diag.keepAliveEnabled = _keepAliveEnabled;
        diag.keepAliveActive = _keepAliveActive.load();
        diag.applyingProfile = _latencyApplyInFlight.load();
        diag.stereoEndpointGuid = ExtractEndpointGuid(_airPodsStereoRenderId);
        diag.containerId = _trackedContainerId;

        if (!diag.stereoEndpointGuid.isEmpty()) {
            const std::wstring fxSubKey =
                L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render\\" +
                diag.stereoEndpointGuid.toStdWString() + L"\\FxProperties";
            const auto disableFx = ReadHklmDword64(fxSubKey, kDisableSysFxValueName);
            diag.enhancementsEnabled = !disableFx.has_value() || disableFx.value() == 0;
        }

        // Probe live shared-mode engine period without initializing/waking an audio stream.
        if (_stereoActive && !_airPodsStereoRenderId.isEmpty() && _enumerator) {
            Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
            const std::wstring wideId = _airPodsStereoRenderId.toStdWString();
            if (SUCCEEDED(_enumerator->GetDevice(wideId.c_str(), dev.ReleaseAndAddressOf())) && dev)
            {
                Support::OS::Windows::Com::UniquePtr<IAudioClient3> client3;
                if (SUCCEEDED(dev->Activate(
                        __uuidof(IAudioClient3), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void **>(client3.ReleaseAndAddressOf()))) &&
                    client3)
                {
                    REFERENCE_TIME defaultPeriod = 0;
                    REFERENCE_TIME minPeriod = 0;
                    if (SUCCEEDED(client3->GetDevicePeriod(&defaultPeriod, &minPeriod))) {
                        diag.enginePeriodMs = static_cast<double>(defaultPeriod) / 10000.0;
                        diag.minPeriodMs = static_cast<double>(minPeriod) / 10000.0;
                    }

                    WAVEFORMATEX *mixFmt = nullptr;
                    if (SUCCEEDED(client3->GetMixFormat(&mixFmt)) && mixFmt != nullptr) {
                        UINT32 curFrames = 0;
                        WAVEFORMATEX *curFmt = nullptr;
                        if (SUCCEEDED(
                                client3->GetCurrentSharedModeEnginePeriod(&curFmt, &curFrames)) &&
                            curFmt != nullptr && curFmt->nSamplesPerSec > 0 && curFrames > 0)
                        {
                            diag.enginePeriodMs = (static_cast<double>(curFrames) * 1000.0) /
                                                  static_cast<double>(curFmt->nSamplesPerSec);
                            CoTaskMemFree(curFmt);
                        }
                        else if (curFmt != nullptr) {
                            CoTaskMemFree(curFmt);
                        }
                        CoTaskMemFree(mixFmt);
                    }
                }
            }
        }

        // Round cleanly to 0.1 ms so floating-point noise never triggers spurious UI updates.
        diag.enginePeriodMs = std::round(diag.enginePeriodMs * 10.0) / 10.0;
        diag.minPeriodMs = std::round(diag.minPeriodMs * 10.0) / 10.0;

        bool changed = false;
        {
            std::lock_guard<std::mutex> lock{_stateMutex};
            if (_latencyDiagnostics != diag) {
                _latencyDiagnostics = diag;
                changed = true;
            }
        }

        if (changed || forceEmit) {
            QMetaObject::invokeMethod(
                _owner, [owner = _owner] { emit owner->LatencyDiagnosticsChanged(); },
                Qt::QueuedConnection);
        }
    }

    void DetachSessionWatchers()
    {
        for (auto *session : _watchedSessions) {
            session->UnregisterAudioSessionNotification(this);
            session->Release();
        }
        _watchedSessions.clear();

        for (auto *mgr : _hfpSessionManagers) {
            mgr->UnregisterSessionNotification(this);
            mgr->Release();
        }
        _hfpSessionManagers.clear();
    }

    bool IsAirPodsEndpointId(const QString &endpointId) const
    {
        if (endpointId.isEmpty()) {
            return false;
        }
        if (endpointId == _airPodsStereoRenderId || endpointId == _airPodsHfpRenderId ||
            endpointId == _airPodsHfpCaptureId)
        {
            return true;
        }
        if (_trackedContainerId.isEmpty() || !_enumerator) {
            return false;
        }
        Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
        const std::wstring wideId = endpointId.toStdWString();
        if (FAILED(_enumerator->GetDevice(wideId.c_str(), dev.ReleaseAndAddressOf())) || !dev) {
            return false;
        }
        Support::OS::Windows::Com::UniquePtr<IPropertyStore> store;
        if (FAILED(dev->OpenPropertyStore(STGM_READ, store.ReleaseAndAddressOf())) || !store) {
            return false;
        }
        return NormalizeGuid(ReadStringProp(store.operator->(), PKEY_Device_ContainerId)) ==
               _trackedContainerId;
    }

    bool IsEndpointActive(const QString &endpointId) const
    {
        if (endpointId.isEmpty() || !_enumerator) {
            return false;
        }
        Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
        const std::wstring wideId = endpointId.toStdWString();
        if (FAILED(_enumerator->GetDevice(wideId.c_str(), dev.ReleaseAndAddressOf())) || !dev) {
            return false;
        }
        DWORD state = 0;
        return SUCCEEDED(dev->GetState(&state)) && state == DEVICE_STATE_ACTIVE;
    }

    void RememberCurrentNonAirPodsOutput()
    {
        if (!_enumerator) {
            return;
        }
        const QString currentDefault =
            GetDefaultEndpointId(_enumerator.operator->(), eRender, eConsole);
        if (!currentDefault.isEmpty() && !IsAirPodsEndpointId(currentDefault)) {
            _previousOutputId = currentDefault;
        }
    }

    bool SwitchToAirPodsStereoInternal()
    {
        if (_airPodsStereoRenderId.isEmpty() || !_stereoActive || !_enumerator) {
            return false;
        }
        RememberCurrentNonAirPodsOutput();
        if (!SetDefaultAudioEndpoint(_airPodsStereoRenderId, true)) {
            return false;
        }
        _routedToAirPods = true;
        _pendingSwitchToAirPods = false;
        LOG(Info, "AudioControl: switched default playback output to AirPods Stereo.");
        if (_volumeCapEnabled) {
            ApplyVolumeCapInternal();
        }
        if (!_preferredMicId.isEmpty()) {
            EnforcePreferredMicInternal();
        }
        return true;
    }

    bool RestorePreviousOutputInternal()
    {
        _routedToAirPods = false;
        if (_previousOutputId.isEmpty() || !IsEndpointActive(_previousOutputId)) {
            return false;
        }
        if (_enumerator) {
            const QString currentDefault =
                GetDefaultEndpointId(_enumerator.operator->(), eRender, eConsole);
            if (currentDefault.compare(_previousOutputId, Qt::CaseInsensitive) == 0) {
                return true;
            }
        }
        const bool ok = SetDefaultAudioEndpoint(_previousOutputId, true);
        if (ok) {
            LOG(Info, "AudioControl: restored previous playback output.");
        }
        return ok;
    }

    void RebindTrackedEndpoints()
    {
        DetachSessionWatchers();

        const bool wasStereoActive = _stereoActive;
        _airPodsStereoRenderId.clear();
        _airPodsHfpRenderId.clear();
        _airPodsHfpCaptureId.clear();
        _stereoActive = false;

        if (_trackedContainerId.isEmpty() || !_enumerator) {
            UpdateKeepAliveState();
            RefreshLatencyDiagnosticsInternal();
            PublishProfile(Core::AirPods::AudioProfile::Unknown, {});
            return;
        }

        int bestStereoRank = 0;
        int bestHfpRenderRank = 0;
        int bestHfpCaptureRank = 0;

        const auto inspectFlow = [&](EDataFlow flow) {
            Support::OS::Windows::Com::UniquePtr<IMMDeviceCollection> collection;
            if (FAILED(_enumerator->EnumAudioEndpoints(
                    flow, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED,
                    collection.ReleaseAndAddressOf())) ||
                !collection)
            {
                return;
            }

            UINT count = 0;
            if (FAILED(collection->GetCount(&count))) {
                return;
            }

            for (UINT i = 0; i < count; ++i) {
                Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
                if (FAILED(collection->Item(i, dev.ReleaseAndAddressOf())) || !dev) {
                    continue;
                }

                Support::OS::Windows::Com::UniquePtr<IPropertyStore> store;
                if (FAILED(dev->OpenPropertyStore(STGM_READ, store.ReleaseAndAddressOf())) ||
                    !store)
                {
                    continue;
                }

                const QString container =
                    NormalizeGuid(ReadStringProp(store.operator->(), PKEY_Device_ContainerId));
                if (container != _trackedContainerId) {
                    continue;
                }

                LPWSTR rawId = nullptr;
                if (FAILED(dev->GetId(&rawId)) || rawId == nullptr) {
                    continue;
                }
                const QString endpointId = QString::fromWCharArray(rawId);
                CoTaskMemFree(rawId);

                DWORD state = 0;
                dev->GetState(&state);
                const bool active = state == DEVICE_STATE_ACTIVE;
                const int rank = EndpointStateRank(state);

                const QString friendly =
                    ReadStringProp(store.operator->(), PKEY_Device_FriendlyName);
                const QString iface =
                    ReadStringProp(store.operator->(), PKEY_DeviceInterface_FriendlyName);

                if (flow == eCapture) {
                    if (rank > bestHfpCaptureRank) {
                        bestHfpCaptureRank = rank;
                        _airPodsHfpCaptureId = endpointId;
                    }
                    if (active) {
                        AttachSessionManager(dev.operator->());
                    }
                }
                else if (LooksLikeHandsFree(friendly, iface)) {
                    if (rank > bestHfpRenderRank) {
                        bestHfpRenderRank = rank;
                        _airPodsHfpRenderId = endpointId;
                    }
                    if (active) {
                        AttachSessionManager(dev.operator->());
                    }
                }
                else if (rank > bestStereoRank) {
                    bestStereoRank = rank;
                    _airPodsStereoRenderId = endpointId;
                    _stereoActive = active;
                }
            }
        };

        inspectFlow(eRender);
        inspectFlow(eCapture);

        if (_stereoActive) {
            const bool pendingNow = _pendingSwitchToAirPods &&
                                    std::chrono::steady_clock::now() <= _pendingSwitchDeadline;
            if (pendingNow || (!wasStereoActive && _autoSwitchOnConnect)) {
                SwitchToAirPodsStereoInternal();
            }
            else {
                if (_volumeCapEnabled) {
                    ApplyVolumeCapInternal();
                }
                if (!_preferredMicId.isEmpty()) {
                    EnforcePreferredMicInternal();
                }
            }
        }
        else if (wasStereoActive) {
            _pendingSwitchToAirPods = false;
            if (_routedToAirPods && _autoSwitchOnConnect) {
                RestorePreviousOutputInternal();
            }
        }

        UpdateKeepAliveState();
        RefreshLatencyDiagnosticsInternal();
        RefreshHfpSessionState();
    }

    void AttachSessionManager(IMMDevice *endpoint)
    {
        IAudioSessionManager2 *mgr = nullptr;
        if (SUCCEEDED(endpoint->Activate(
                __uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                reinterpret_cast<void **>(&mgr))) &&
            mgr != nullptr)
        {
            if (SUCCEEDED(mgr->RegisterSessionNotification(this))) {
                _hfpSessionManagers.push_back(mgr);
            }
            else {
                mgr->Release();
            }
        }
    }

    void RefreshHfpSessionState()
    {
        for (auto *session : _watchedSessions) {
            session->UnregisterAudioSessionNotification(this);
            session->Release();
        }
        _watchedSessions.clear();

        for (auto *mgr : _hfpSessionManagers) {
            Support::OS::Windows::Com::UniquePtr<IAudioSessionEnumerator> enumerator;
            if (FAILED(mgr->GetSessionEnumerator(enumerator.ReleaseAndAddressOf())) || !enumerator)
            {
                continue;
            }
            int count = 0;
            if (FAILED(enumerator->GetCount(&count))) {
                continue;
            }
            for (int i = 0; i < count; ++i) {
                IAudioSessionControl *control = nullptr;
                if (SUCCEEDED(enumerator->GetSession(i, &control)) && control != nullptr) {
                    if (SUCCEEDED(control->RegisterAudioSessionNotification(this))) {
                        _watchedSessions.push_back(control);
                    }
                    else {
                        control->Release();
                    }
                }
            }
        }

        EvaluateActiveHfpSessions();
    }

    void EvaluateActiveHfpSessions()
    {
        for (auto *session : _watchedSessions) {
            AudioSessionState state = AudioSessionStateInactive;
            if (SUCCEEDED(session->GetState(&state)) && state == AudioSessionStateActive) {
                QString processName;
                Support::OS::Windows::Com::UniquePtr<IAudioSessionControl2> ctrl2;
                if (SUCCEEDED(session->QueryInterface(
                        __uuidof(IAudioSessionControl2),
                        reinterpret_cast<void **>(ctrl2.ReleaseAndAddressOf()))) &&
                    ctrl2)
                {
                    DWORD pid = 0;
                    if (SUCCEEDED(ctrl2->GetProcessId(&pid))) {
                        processName = ResolveProcessName(pid);
                    }
                }
                PublishProfile(Core::AirPods::AudioProfile::HandsFree, processName);
                return;
            }
        }

        if (!_airPodsStereoRenderId.isEmpty() && _enumerator) {
            Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
            const std::wstring wideId = _airPodsStereoRenderId.toStdWString();
            DWORD state = 0;
            if (SUCCEEDED(_enumerator->GetDevice(wideId.c_str(), dev.ReleaseAndAddressOf())) &&
                dev && SUCCEEDED(dev->GetState(&state)) && state == DEVICE_STATE_ACTIVE)
            {
                PublishProfile(Core::AirPods::AudioProfile::Stereo, {});
                return;
            }
        }

        PublishProfile(Core::AirPods::AudioProfile::Unknown, {});
    }

    bool EnforcePreferredMicInternal()
    {
        if (_preferredMicId.isEmpty() || !_enumerator) {
            return false;
        }

        // Only intervene when Windows routed Console or Communications capture onto the
        // AirPods HFP microphone (or when explicitly requested while AirPods are active).
        const QString curConsole =
            GetDefaultEndpointId(_enumerator.operator->(), eCapture, eConsole);
        const QString curComm =
            GetDefaultEndpointId(_enumerator.operator->(), eCapture, eCommunications);

        const bool airPodsTookMic =
            !_airPodsHfpCaptureId.isEmpty() &&
            (curConsole == _airPodsHfpCaptureId || curComm == _airPodsHfpCaptureId);
        if (!airPodsTookMic) {
            return false;
        }

        // Verify the preferred mic is currently active before switching to it.
        Support::OS::Windows::Com::UniquePtr<IMMDevice> prefDev;
        const std::wstring widePref = _preferredMicId.toStdWString();
        DWORD state = 0;
        if (FAILED(_enumerator->GetDevice(widePref.c_str(), prefDev.ReleaseAndAddressOf())) ||
            !prefDev || FAILED(prefDev->GetState(&state)) || state != DEVICE_STATE_ACTIVE)
        {
            return false;
        }

        LOG(Info, "AudioControl: AirPods HFP took default microphone; restoring preferred mic.");
        return SetDefaultAudioEndpoint(_preferredMicId, true);
    }

    void ApplyVolumeCapInternal()
    {
        if (!_volumeCapEnabled || _airPodsStereoRenderId.isEmpty() || !_enumerator) {
            return;
        }

        Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
        const std::wstring wideId = _airPodsStereoRenderId.toStdWString();
        if (FAILED(_enumerator->GetDevice(wideId.c_str(), dev.ReleaseAndAddressOf())) || !dev) {
            return;
        }

        DWORD state = 0;
        if (FAILED(dev->GetState(&state)) || state != DEVICE_STATE_ACTIVE) {
            return;
        }

        Support::OS::Windows::Com::UniquePtr<IAudioEndpointVolume> vol;
        if (FAILED(dev->Activate(
                __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                reinterpret_cast<void **>(vol.ReleaseAndAddressOf()))) ||
            !vol)
        {
            return;
        }

        float current = 0.0f;
        if (SUCCEEDED(vol->GetMasterVolumeLevelScalar(&current))) {
            const float limit = static_cast<float>(_volumeCapPercent) / 100.0f;
            if (current > limit + 0.005f) {
                LOG(Info, "AudioControl: capping AirPods volume from {:.0f}% to {}%.",
                    current * 100.0f, _volumeCapPercent);
                vol->SetMasterVolumeLevelScalar(limit, nullptr);
            }
        }
    }

    bool OpenDefaultRenderVolume(Support::OS::Windows::Com::UniquePtr<IAudioEndpointVolume> &out)
    {
        if (!_enumerator) {
            return false;
        }
        Support::OS::Windows::Com::UniquePtr<IMMDevice> dev;
        if (FAILED(_enumerator->GetDefaultAudioEndpoint(
                eRender, eConsole, dev.ReleaseAndAddressOf())) ||
            !dev)
        {
            return false;
        }
        return SUCCEEDED(dev->Activate(
                   __uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                   reinterpret_cast<void **>(out.ReleaseAndAddressOf()))) &&
               static_cast<bool>(out);
    }

    bool IsDuckVolumeStillOurs()
    {
        if (!_duckVolume) {
            return false;
        }
        float current = 0.0f;
        if (FAILED(_duckVolume->GetMasterVolumeLevelScalar(&current))) {
            return false;
        }
        constexpr float kTolerance = 0.012f;
        return std::abs(current - _duckLastSetVolume) <= kTolerance;
    }

    void EndDuckEpisode()
    {
        _duckActive = false;
        _duckLevel = 0;
        _duckVolume = nullptr;
        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            _duckQuietDeadline.reset();
        }
    }

    void RestoreConversationDuckingInternal()
    {
        if (!_duckActive) {
            return;
        }
        if (IsDuckVolumeStillOurs()) {
            _duckVolume->SetMasterVolumeLevelScalar(_duckStartVolume, nullptr);
            LOG(Info, "AudioControl: Conversation Awareness restored volume to {:.0f}%.",
                _duckStartVolume * 100.0f);
        }
        else {
            LOG(Info,
                "AudioControl: Conversation Awareness restore skipped (volume changed manually).");
        }
        EndDuckEpisode();
    }

    void HandleConversationAwarenessLevelInternal(uint8_t level)
    {
        // Level 0, 8, 9 -> restore normal volume.
        if (level == 0 || level >= 8) {
            RestoreConversationDuckingInternal();
            return;
        }

        // Never duck while in a Hands-Free call.
        if (CurrentProfile() == Core::AirPods::AudioProfile::HandsFree) {
            RestoreConversationDuckingInternal();
            return;
        }

        if (!_duckActive) {
            // Only level 1 or 2 starts a ducking episode; intermediate recovery steps (3..7)
            // never start a fresh episode on their own.
            if (level > 2) {
                return;
            }
            if (!OpenDefaultRenderVolume(_duckVolume)) {
                return;
            }
            float start = 0.0f;
            if (FAILED(_duckVolume->GetMasterVolumeLevelScalar(&start))) {
                EndDuckEpisode();
                return;
            }
            _duckStartVolume = start;
            _duckActive = true;
            LOG(Info, "AudioControl: Conversation Awareness ducking started (baseline {:.0f}%).",
                start * 100.0f);
        }
        else if (!IsDuckVolumeStillOurs()) {
            LOG(Info, "AudioControl: Conversation Awareness ended (user changed volume manually).");
            EndDuckEpisode();
            return;
        }

        // Apple recovery curve: levels 1..2 = 25% of start volume; levels 3..7 step linearly back
        // toward start in 6 even increments (1/6 .. 5/6).
        static constexpr std::array<float, 8> kRecovery{
            0.0f, 0.0f, 0.0f, 1.0f / 6.0f, 2.0f / 6.0f, 3.0f / 6.0f, 4.0f / 6.0f, 5.0f / 6.0f};
        const float duckFloor = _duckStartVolume * 0.25f;
        const float target =
            std::clamp(duckFloor + (_duckStartVolume - duckFloor) * kRecovery[level], 0.0f, 1.0f);
        _duckLevel = level;
        if (SUCCEEDED(_duckVolume->SetMasterVolumeLevelScalar(target, nullptr))) {
            _duckLastSetVolume = target;
        }

        {
            std::lock_guard<std::mutex> lock{_queueMutex};
            if (level > 2) {
                _duckQuietDeadline = std::chrono::steady_clock::now() + std::chrono::seconds{2};
            }
            else {
                _duckQuietDeadline.reset();
            }
        }
    }

    void PublishProfile(Core::AirPods::AudioProfile profile, const QString &processName)
    {
        {
            std::lock_guard<std::mutex> lock{_stateMutex};
            if (_profile == profile && _hfpProcess == processName) {
                return;
            }
            _profile = profile;
            _hfpProcess = processName;
        }

        QMetaObject::invokeMethod(
            _owner,
            [owner = _owner, profile, processName] {
                emit owner->AudioProfileChanged(profile, processName);
            },
            Qt::QueuedConnection);
    }

    void EmitEndpointsChanged()
    {
        QMetaObject::invokeMethod(
            _owner, [owner = _owner] { emit owner->EndpointsChanged(); }, Qt::QueuedConnection);
    }

    Service *_owner{nullptr};
    std::atomic<ULONG> _refCount{1};
    HANDLE _shutdownEvent{nullptr};

    std::mutex _queueMutex;
    std::condition_variable _cv;
    std::deque<std::function<void()>> _tasks;
    bool _stopping{false};
    std::thread _worker;

    mutable std::mutex _stateMutex;
    Core::AirPods::AudioProfile _profile{Core::AirPods::AudioProfile::Unknown};
    QString _hfpProcess;
    LatencyDiagnostics _latencyDiagnostics;

    // Worker-thread-only members:
    Support::OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> _enumerator;
    bool _registeredDeviceEvents{false};
    QString _trackedContainerId;
    QString _airPodsStereoRenderId;
    QString _airPodsHfpRenderId;
    QString _airPodsHfpCaptureId;
    QString _preferredMicId;
    QString _previousOutputId;
    bool _stereoActive{false};
    bool _autoSwitchOnConnect{true};
    bool _routedToAirPods{false};
    bool _pendingSwitchToAirPods{false};
    std::chrono::steady_clock::time_point _pendingSwitchDeadline{};
    bool _volumeCapEnabled{false};
    uint32_t _volumeCapPercent{80};
    bool _keepAliveEnabled{false};
    bool _airPodsWorn{false};
    std::atomic<bool> _keepAliveActive{false};
    std::atomic<bool> _latencyApplyInFlight{false};
    HANDLE _keepAliveStopEvent{nullptr};
    QString _keepAliveEndpointId;
    std::thread _keepAliveThread;
    std::vector<IAudioSessionManager2 *> _hfpSessionManagers;
    std::vector<IAudioSessionControl *> _watchedSessions;
    // Conversation Awareness smooth media ducking state (worker thread + quiet deadline under
    // `_queueMutex`).
    bool _duckActive{false};
    uint8_t _duckLevel{0};
    float _duckStartVolume{0.0f};
    float _duckLastSetVolume{0.0f};
    Support::OS::Windows::Com::UniquePtr<IAudioEndpointVolume> _duckVolume;
    std::optional<std::chrono::steady_clock::time_point> _duckQuietDeadline;
};

Service::Service(QObject *parent) : QObject{parent}, _impl{std::make_unique<Impl>(this)} {}

Service::~Service() = default;

void Service::SetTrackedContainerId(const QString &containerId)
{
    _impl->SetTrackedContainerId(containerId);
}

void Service::SetPreferredMicrophoneId(const QString &endpointId)
{
    _impl->SetPreferredMicrophoneId(endpointId);
}

void Service::SetVolumeCap(bool enabled, uint32_t maxPercent)
{
    _impl->SetVolumeCap(enabled, maxPercent);
}

void Service::SetKeepAliveEnabled(bool enabled)
{
    _impl->SetKeepAliveEnabled(enabled);
}

void Service::SetAirPodsWorn(bool worn)
{
    _impl->SetAirPodsWorn(worn);
}

void Service::SetAutoSwitchOnConnect(bool enabled)
{
    _impl->SetAutoSwitchOnConnect(enabled);
}

void Service::OnConversationAwarenessLevel(uint8_t level)
{
    _impl->OnConversationAwarenessLevel(level);
}

void Service::RestoreConversationDucking()
{
    _impl->RestoreConversationDucking();
}

bool Service::SwitchOutputToAirPods()
{
    return _impl->SwitchOutputToAirPods();
}

void Service::RequestSwitchOutputToAirPods()
{
    _impl->RequestSwitchOutputToAirPods();
}

bool Service::RestorePreviousOutput()
{
    return _impl->RestorePreviousOutput();
}

bool Service::EnforcePreferredMicrophone()
{
    return _impl->EnforcePreferredMicrophone();
}

void Service::ApplyVolumeCapNow()
{
    _impl->ApplyVolumeCapNow();
}

bool Service::ApplyLatencyProfileElevated(bool lowLatencySbc)
{
    return _impl->ApplyLatencyProfileElevated(lowLatencySbc);
}

void Service::SuspendKeepAliveTemporarily()
{
    _impl->SuspendKeepAliveTemporarily();
}

void Service::RefreshLatencyDiagnostics()
{
    _impl->RefreshLatencyDiagnostics();
}

std::vector<AudioEndpoint> Service::ListCaptureEndpoints() const
{
    return _impl->ListEndpoints(eCapture);
}

std::vector<AudioEndpoint> Service::ListRenderEndpoints() const
{
    return _impl->ListEndpoints(eRender);
}

Core::AirPods::AudioProfile Service::CurrentProfile() const
{
    return _impl->CurrentProfile();
}

QString Service::CurrentHfpProcess() const
{
    return _impl->CurrentHfpProcess();
}

LatencyDiagnostics Service::GetLatencyDiagnostics() const
{
    return _impl->GetLatencyDiagnostics();
}

LatencyProfileApplyResult ApplyLatencyProfileInProcess(
    bool lowLatencySbc, const QString &endpointGuid, const QString &containerId)
{
    LatencyProfileApplyResult result;

    // 1. Switch Microsoft BthA2dp.sys codec preference: 0 = SBC (low latency), 1 = AAC (default).
    const DWORD aacValue = lowLatencySbc ? 0u : 1u;
    if (!WriteHklmDword64(kBthA2dpParamsKey, L"BluetoothAacEnable", aacValue)) {
        return result;
    }

    // 2. Disable or restore SysFx Audio Enhancements APO on every matching AirPods/Beats A2DP
    //    render endpoint via Windows' own `IPolicyConfig::SetPropertyValue` (`Audiosrv`/`SYSTEM`),
    //    plus direct native 64-bit registry writes for any offline endpoints.
    const DWORD disableSysFx = lowLatencySbc ? 1u : 0u;
    const QString validatedGuid = ValidateBracedGuid(endpointGuid);
    const QString normalizedContainer = NormalizeGuid(containerId);
    SetAudioEnhancementsViaPolicyConfig(lowLatencySbc, validatedGuid, normalizedContainer);

    if (!validatedGuid.isEmpty()) {
        const std::wstring fxSubKey = std::wstring{kMmDevicesRenderKey} + L"\\" +
                                      validatedGuid.toStdWString() + L"\\FxProperties";
        WriteHklmDword64(fxSubKey, kDisableSysFxValueName, disableSysFx);
    }

    HKEY renderRoot = nullptr;
    if (RegOpenKeyExW(
            HKEY_LOCAL_MACHINE, kMmDevicesRenderKey, 0,
            KEY_ENUMERATE_SUB_KEYS | KEY_READ | KEY_WOW64_64KEY, &renderRoot) == ERROR_SUCCESS &&
        renderRoot != nullptr)
    {
        wchar_t subName[128]{};
        for (DWORD idx = 0;; ++idx) {
            DWORD nameLen = static_cast<DWORD>(std::size(subName));
            const LONG enumRes = RegEnumKeyExW(
                renderRoot, idx, subName, &nameLen, nullptr, nullptr, nullptr, nullptr);
            if (enumRes == ERROR_NO_MORE_ITEMS) {
                break;
            }
            if (enumRes != ERROR_SUCCESS) {
                continue;
            }

            const QString guidKey = QString::fromWCharArray(subName, static_cast<int>(nameLen));
            if (!validatedGuid.isEmpty() &&
                guidKey.compare(validatedGuid, Qt::CaseInsensitive) == 0)
            {
                continue;
            }

            const std::wstring propsPath =
                std::wstring{kMmDevicesRenderKey} + L"\\" + subName + L"\\Properties";
            const auto devName =
                ReadHklmString64(propsPath, kPropDeviceNameValue).value_or(QString{});
            const auto friendly =
                ReadHklmString64(propsPath, kPropEndpointFriendlyValue).value_or(QString{});
            const bool isHandsFree = LooksLikeHandsFree(devName, friendly);
            if (!isHandsFree && IsAppleAudioDeviceName(devName)) {
                const std::wstring fxSubKey =
                    std::wstring{kMmDevicesRenderKey} + L"\\" + subName + L"\\FxProperties";
                WriteHklmDword64(fxSubKey, kDisableSysFxValueName, disableSysFx);
            }
        }
        RegCloseKey(renderRoot);
    }

    // 3. Best-effort USB Bluetooth radio power-saving & Selective Suspend policy via native COM
    //    WMI and native 64-bit HKLM registry (zero PowerShell).
    UpdateUsbBluetoothPowerPolicyWmi(lowLatencySbc);
    UpdateUsbBluetoothSelectiveSuspendRegistry(lowLatencySbc);

    // 4. Re-negotiate the AirPods/Beats A2DP PnP node via cfgmgr32.dll so the new codec and APO
    //    state take effect immediately without rebooting Windows.
    result.a2dpRenegotiated = RenegotiateAppleA2dpNodes(normalizedContainer);
    result.succeeded = true;
    return result;
}

} // namespace Core::AudioControl
