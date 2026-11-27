// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/NativeSetup.h"
#include "Service/Win/RadioDriverManager.h"
#include "Service/Win/RegistryStore.h"
#include "Service/Win/ServiceLog.h"
#include "Service/Win/WinHandles.h"

#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <regex>
#include <span>
#include <vector>

namespace Service::Win {

namespace {

constexpr wchar_t kServiceName[] = L"AirpodsBridgeService";
constexpr wchar_t kServiceDisplayName[] = L"AirpodsBridge Radio Proxy Service";
constexpr wchar_t kServiceDescription[] =
    L"Provides two-way Apple Accessory Protocol (AAP) control and stereo-safe AirPods audio "
    L"without Test Mode using inbox WinUSB and signed usbip-win2.";
constexpr wchar_t kDataDir[] = L"C:\\ProgramData\\AirpodsBridge";

std::string EscapeJson(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

void WriteProgress(std::string_view status, int percent, std::string_view message)
{
    std::error_code ec;
    std::filesystem::create_directories(kDataDir, ec);
    const std::wstring tmpPath = std::wstring{NativeSetup::kProgressFilePath} + L".tmp";
    {
        std::ofstream f{tmpPath, std::ios::out | std::ios::trunc | std::ios::binary};
        if (f.is_open()) {
            f << "{\n"
              << "  \"status\": \"" << EscapeJson(status) << "\",\n"
              << "  \"percent\": " << percent << ",\n"
              << "  \"message\": \"" << EscapeJson(message) << "\"\n"
              << "}\n";
            f.flush();
        }
    }
    MoveFileExW(tmpPath.c_str(), NativeSetup::kProgressFilePath, MOVEFILE_REPLACE_EXISTING);
    ServiceLog::Write(
        status == "error" ? LogLevel::Error : LogLevel::Info, "setup",
        "[" + std::string{status} + " " + std::to_string(percent) + "%] " + std::string{message});
}

bool HttpGetToBuffer(
    const std::wstring &url, std::vector<uint8_t> &outBody, std::string &errOut,
    const std::function<void(uint64_t, uint64_t)> &onProgress = {})
{
    outBody.clear();
    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256]{};
    wchar_t path[1024]{};
    parts.lpszHostName = host;
    parts.dwHostNameLength = 256;
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = 1024;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        errOut = "Invalid download URL.";
        return false;
    }

    HINTERNET session = WinHttpOpen(
        L"AirpodsBridge-Setup/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
        errOut = "WinHttpOpen failed: " + FormatWin32Error(GetLastError());
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(
        session, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));

    HINTERNET connect = WinHttpConnect(session, host, parts.nPort, 0);
    if (connect == nullptr) {
        errOut = "WinHttpConnect failed: " + FormatWin32Error(GetLastError());
        WinHttpCloseHandle(session);
        return false;
    }

    const DWORD flags = (parts.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(
        connect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (request == nullptr) {
        errOut = "WinHttpOpenRequest failed: " + FormatWin32Error(GetLastError());
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    WinHttpSetTimeouts(request, 15000, 15000, 30000, 60000);
    const wchar_t *headers = L"Accept: application/vnd.github+json\r\n";

    bool ok = WinHttpSendRequest(request, headers, static_cast<DWORD>(-1L), nullptr, 0, 0, 0) &&
              WinHttpReceiveResponse(request, nullptr);
    if (!ok) {
        errOut = "HTTP request failed: " + FormatWin32Error(GetLastError());
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);
    if (statusCode < 200 || statusCode >= 300) {
        errOut = "Download server returned HTTP " + std::to_string(statusCode) + ".";
        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return false;
    }

    wchar_t clBuf[64]{};
    DWORD clSize = sizeof(clBuf);
    uint64_t totalExpected = 0;
    if (WinHttpQueryHeaders(
            request, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, clBuf, &clSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        totalExpected = _wcstoui64(clBuf, nullptr, 10);
    }

    uint8_t chunk[32768]{};
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(request, &avail) && avail > 0) {
        while (avail > 0) {
            const DWORD toRead = std::min<DWORD>(avail, sizeof(chunk));
            DWORD got = 0;
            if (!WinHttpReadData(request, chunk, toRead, &got) || got == 0) {
                break;
            }
            outBody.insert(outBody.end(), chunk, chunk + got);
            avail -= got;
            if (onProgress) {
                onProgress(outBody.size(), totalExpected);
            }
        }
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return !outBody.empty();
}

std::string ComputeSha256Hex(std::span<const uint8_t> data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return {};
    }
    DWORD objLen = 0;
    DWORD got = 0;
    BCryptGetProperty(
        alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &got, 0);
    std::vector<uint8_t> obj(objLen);
    std::array<uint8_t, 32> digest{};
    bool ok = false;
    if (BCryptCreateHash(alg, &hash, obj.data(), objLen, nullptr, 0, 0) == 0) {
        if (BCryptHashData(
                hash, const_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0) == 0 &&
            BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0)
        {
            ok = true;
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) {
        return {};
    }

    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(64);
    for (uint8_t b : digest) {
        hex.push_back(kHex[b >> 4]);
        hex.push_back(kHex[b & 0x0Fu]);
    }
    return hex;
}

bool IsArm64Host() noexcept
{
#if defined(_M_ARM64)
    return true;
#else
    using IsWow64Process2Fn = BOOL(WINAPI *)(HANDLE, USHORT *, USHORT *);
    if (const HMODULE k32 = GetModuleHandleW(L"kernel32.dll"); k32 != nullptr) {
        if (const auto fn =
                reinterpret_cast<IsWow64Process2Fn>(GetProcAddress(k32, "IsWow64Process2"));
            fn != nullptr)
        {
            USHORT processMachine = 0;
            USHORT nativeMachine = 0;
            if (fn(GetCurrentProcess(), &processMachine, &nativeMachine)) {
                return nativeMachine == IMAGE_FILE_MACHINE_ARM64;
            }
        }
    }
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    return si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64;
#endif
}

bool DownloadAndInstallUsbipWin2(bool &rebootNeeded, std::string &errOut)
{
    rebootNeeded = false;
    if (RadioDriverManager::IsUsbipDriverReady()) {
        WriteProgress("running", 45, "Signed usbip-win2 driver is already installed.");
        return true;
    }

    const bool arm64 = IsArm64Host();
    const char *archTag = arm64 ? "arm64" : "x64";

    WriteProgress(
        "running", 18,
        std::string{"Checking latest Microsoft-signed usbip-win2 ("} + archTag + ") release...");
    std::vector<uint8_t> jsonBytes;
    if (!HttpGetToBuffer(
            L"https://api.github.com/repos/vadimgrn/usbip-win2/releases/latest", jsonBytes, errOut))
    {
        return false;
    }

    const std::string json{reinterpret_cast<const char *>(jsonBytes.data()), jsonBytes.size()};

    // Find the matching architecture (x64 or arm64) installer asset URL and optional SHA-256.
    const std::regex urlRe{
        arm64
            ? R"re("browser_download_url"\s*:\s*"(https://github\.com/vadimgrn/usbip-win2/releases/download/[^"]+/USBip-[^"]+-arm64\.exe)")re"
            : R"re("browser_download_url"\s*:\s*"(https://github\.com/vadimgrn/usbip-win2/releases/download/[^"]+/USBip-[^"]+-x64\.exe)")re"};
    std::smatch urlMatch;
    if (!std::regex_search(json, urlMatch, urlRe)) {
        errOut = std::string{"Could not locate "} + archTag +
                 " installer in vadimgrn/usbip-win2 release metadata.";
        return false;
    }
    const std::string downloadUrl = urlMatch[1].str();

    std::string expectedSha256;
    const size_t assetPos = static_cast<size_t>(urlMatch.position());
    const size_t windowStart = assetPos > 1024 ? assetPos - 1024 : 0;
    const std::string assetSlice =
        json.substr(windowStart, std::min<size_t>(2048, json.size() - windowStart));
    static const std::regex kDigestRe{R"re("digest"\s*:\s*"sha256:([0-9a-fA-F]{64})")re"};
    std::smatch digestMatch;
    if (std::regex_search(assetSlice, digestMatch, kDigestRe)) {
        expectedSha256 = digestMatch[1].str();
        std::transform(
            expectedSha256.begin(), expectedSha256.end(), expectedSha256.begin(),
            [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    }

    WriteProgress("running", 25, "Downloading signed usbip-win2 driver package...");
    std::vector<uint8_t> installerBytes;
    int lastPct = 25;
    if (!HttpGetToBuffer(
            Utf8ToWide(downloadUrl), installerBytes, errOut, [&](uint64_t done, uint64_t total) {
                if (total > 0) {
                    const int pct = 25 + static_cast<int>((done * 30ull) / total);
                    if (pct >= lastPct + 5) {
                        lastPct = pct;
                        WriteProgress(
                            "running", pct,
                            "Downloading signed usbip-win2 (" +
                                std::to_string(done / (1024 * 1024)) + " / " +
                                std::to_string(total / (1024 * 1024)) + " MB)...");
                    }
                }
            }))
    {
        return false;
    }

    if (!expectedSha256.empty()) {
        const std::string actual = ComputeSha256Hex(installerBytes);
        if (actual != expectedSha256) {
            errOut = "SHA-256 verification failed for downloaded usbip-win2 installer.";
            return false;
        }
    }

    wchar_t tempDir[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempDir);
    const std::filesystem::path installerPath =
        std::filesystem::path{tempDir} /
        (arm64 ? L"AirpodsBridge-USBip-arm64.exe" : L"AirpodsBridge-USBip-x64.exe");
    {
        std::ofstream out{installerPath, std::ios::out | std::ios::trunc | std::ios::binary};
        if (!out.is_open()) {
            errOut = "Failed to stage usbip-win2 installer to temp directory.";
            return false;
        }
        out.write(
            reinterpret_cast<const char *>(installerBytes.data()),
            static_cast<std::streamsize>(installerBytes.size()));
    }

    WriteProgress(
        "running", 60,
        "Installing Microsoft-signed usbip-win2 (USB devices may briefly reconnect)...");

    const std::wstring usbipLog = std::wstring{kDataDir} + L"\\usbip-install.log";
    std::wstring cmdLine = L"\"" + installerPath.wstring() +
                           L"\" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART "
                           L"/MERGETASKS=\"!desktopicon\" /LOG=\"" +
                           usbipLog + L"\"";

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(
            nullptr, cmdLine.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
            &si, &pi))
    {
        errOut = "Failed to launch usbip-win2 installer: " + FormatWin32Error(GetLastError());
        return false;
    }
    UniqueHandle proc{pi.hProcess};
    UniqueHandle thr{pi.hThread};

    if (WaitForSingleObject(proc.Get(), 180000) != WAIT_OBJECT_0) {
        TerminateProcess(proc.Get(), 1);
        errOut = "usbip-win2 installer timed out.";
        return false;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(proc.Get(), &exitCode);
    DeleteFileW(installerPath.c_str());

    if (exitCode != 0 && exitCode != 3010) {
        errOut = "usbip-win2 installer exited with code " + std::to_string(exitCode) + ".";
        return false;
    }

    // Check log for Inno Setup's "Need to restart Windows? Yes" (since /NORESTART returns 0).
    if (exitCode == 3010) {
        rebootNeeded = true;
    }
    else if (std::ifstream logF{usbipLog}; logF.is_open()) {
        std::string line;
        while (std::getline(logF, line)) {
            if (line.find("Need to restart Windows? Yes") != std::string::npos ||
                line.find("Process exit code: 3010") != std::string::npos)
            {
                rebootNeeded = true;
                break;
            }
        }
    }

    for (int i = 0; i < 20; ++i) {
        if (RadioDriverManager::IsUsbipDriverReady()) {
            return true;
        }
        Sleep(300);
    }

    errOut = "usbip-win2 finished installing, but usbip2_ude or usbip.exe was not found.";
    return false;
}

bool StopAndWaitService(SC_HANDLE scm, int maxWaitMs = 25000)
{
    UniqueScHandle svc{OpenServiceW(scm, kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS)};
    if (!svc.Valid()) {
        return true;
    }
    SERVICE_STATUS st{};
    if (QueryServiceStatus(svc.Get(), &st) && st.dwCurrentState == SERVICE_STOPPED) {
        return true;
    }
    ControlService(svc.Get(), SERVICE_CONTROL_STOP, &st);
    const int iterations = std::max(1, maxWaitMs / 250);
    for (int i = 0; i < iterations; ++i) {
        if (!QueryServiceStatus(svc.Get(), &st) || st.dwCurrentState == SERVICE_STOPPED) {
            return true;
        }
        Sleep(250);
    }
    ServiceLog::Write(
        LogLevel::Error, "setup",
        "Timed out after " + std::to_string(maxWaitMs) +
            " ms waiting for AirpodsBridgeService to stop.");
    return false;
}

std::filesystem::path TargetServiceExePath()
{
    wchar_t pf[MAX_PATH]{};
    ExpandEnvironmentStringsW(L"%ProgramFiles%", pf, MAX_PATH);
    return std::filesystem::path{pf} / L"AirpodsBridge" / L"RadioService" /
           L"AirpodsBridgeService.exe";
}

} // namespace

int NativeSetup::RunEnable()
{
    ServiceLog::Initialize(false);
    WriteProgress("running", 5, "Inspecting Bluetooth adapter compatibility...");

    const auto search = RadioDriverManager::FindPhysicalAdapter();
    if (search.status == AdapterSearchStatus::NonUsbOnly) {
        WriteProgress(
            "error", 100,
            "Your internal Bluetooth radio uses UART/SDIO rather than USB (common on "
            "Snapdragon/ARM laptops) and cannot be proxied. BLE battery and ear detection work "
            "natively; connect a USB Bluetooth adapter to enable two-way ANC and Heart Rate "
            "controls.");
        return 1;
    }
    if (search.status != AdapterSearchStatus::Found || !search.adapter.has_value()) {
        WriteProgress("error", 100, "No physical USB Bluetooth adapter was detected.");
        return 1;
    }

    bool rebootNeeded = false;
    std::string err;
    if (!DownloadAndInstallUsbipWin2(rebootNeeded, err)) {
        WriteProgress("error", 100, err);
        return 1;
    }

    WriteProgress(
        "running", 78,
        std::string{"Registering native "} + (IsArm64Host() ? "ARM64" : "x64") +
            " AirpodsBridgeService...");

    UniqueScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS)};
    if (!scm.Valid()) {
        WriteProgress(
            "error", 100, "Administrator access required: " + FormatWin32Error(GetLastError()));
        return 1;
    }

    if (!StopAndWaitService(scm.Get(), 25000)) {
        WriteProgress(
            "error", 100,
            "Existing AirpodsBridgeService is still stopping. Wait a moment or restart Windows "
            "before enabling again.");
        ServiceLog::Shutdown();
        return 1;
    }

    wchar_t selfPath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, selfPath, MAX_PATH) == 0) {
        WriteProgress("error", 100, "Could not resolve service executable path.");
        ServiceLog::Shutdown();
        return 1;
    }

    const auto targetExe = TargetServiceExePath();
    std::error_code ec;
    std::filesystem::create_directories(targetExe.parent_path(), ec);
    if (!std::filesystem::equivalent(std::filesystem::path{selfPath}, targetExe, ec)) {
        bool copied = false;
        DWORD lastErr = ERROR_SUCCESS;
        for (int attempt = 0; attempt < 10; ++attempt) {
            if (CopyFileW(selfPath, targetExe.c_str(), FALSE)) {
                copied = true;
                break;
            }
            lastErr = GetLastError();
            Sleep(200);
        }
        if (!copied) {
            WriteProgress(
                "error", 100, "Failed to copy service binary: " + FormatWin32Error(lastErr));
            ServiceLog::Shutdown();
            return 1;
        }
    }

    const std::wstring binPath = L"\"" + targetExe.wstring() + L"\"";
    UniqueScHandle svc{OpenServiceW(scm.Get(), kServiceName, SERVICE_ALL_ACCESS)};
    if (!svc.Valid()) {
        svc.Reset(CreateServiceW(
            scm.Get(), kServiceName, kServiceDisplayName, SERVICE_ALL_ACCESS,
            SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL, binPath.c_str(),
            nullptr, nullptr, L"usbip2_ude\0", nullptr, nullptr));
    }
    else {
        ChangeServiceConfigW(
            svc.Get(), SERVICE_WIN32_OWN_PROCESS, SERVICE_AUTO_START, SERVICE_ERROR_NORMAL,
            binPath.c_str(), nullptr, nullptr, L"usbip2_ude\0", nullptr, nullptr,
            kServiceDisplayName);
    }
    if (!svc.Valid()) {
        WriteProgress(
            "error", 100, "Failed to register service: " + FormatWin32Error(GetLastError()));
        return 1;
    }

    SERVICE_DESCRIPTIONW desc{};
    desc.lpDescription = const_cast<LPWSTR>(kServiceDescription);
    ChangeServiceConfig2W(svc.Get(), SERVICE_CONFIG_DESCRIPTION, &desc);

    SERVICE_PRESHUTDOWN_INFO preshutdown{};
    preshutdown.dwPreshutdownTimeout = 15000;
    ChangeServiceConfig2W(svc.Get(), SERVICE_CONFIG_PRESHUTDOWN_INFO, &preshutdown);

    SC_ACTION actions[3] = {
        {SC_ACTION_RESTART, 3000},
        {SC_ACTION_RESTART, 5000},
        {SC_ACTION_NONE, 0},
    };
    SERVICE_FAILURE_ACTIONSW failure{};
    failure.dwResetPeriod = 600;
    failure.cActions = 3;
    failure.lpsaActions = actions;
    ChangeServiceConfig2W(svc.Get(), SERVICE_CONFIG_FAILURE_ACTIONS, &failure);

    RegistryStore::SetUninstalling(false);
    RegistryStore::SetGuardTripped(false);

    WriteProgress("running", 90, "Starting AirpodsBridge Control Engine...");
    if (!StartServiceW(svc.Get(), 0, nullptr)) {
        const DWORD startErr = GetLastError();
        if (startErr != ERROR_SERVICE_ALREADY_RUNNING) {
            WriteProgress("error", 100, "Failed to start service: " + FormatWin32Error(startErr));
            return 1;
        }
    }

    for (int i = 0; i < 20; ++i) {
        SERVICE_STATUS st{};
        if (QueryServiceStatus(svc.Get(), &st) && st.dwCurrentState == SERVICE_RUNNING) {
            break;
        }
        Sleep(250);
    }

    if (rebootNeeded) {
        WriteProgress(
            "reboot_required", 100,
            "Control Engine enabled. Restart Windows once so usbip-win2 finishes updating USB "
            "hubs.");
        ServiceLog::Shutdown();
        return 3010;
    }

    WriteProgress(
        "ok", 100,
        "No-Test-Mode Control Engine is active (WinUSB + signed usbip-win2, Secure Boot ON).");
    ServiceLog::Shutdown();
    return 0;
}

int NativeSetup::RunDisable()
{
    ServiceLog::Initialize(false);
    WriteProgress("running", 15, "Stopping AirpodsBridge Control Engine...");
    RegistryStore::SetUninstalling(true);

    bool serviceStopped = true;
    UniqueScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (scm.Valid()) {
        serviceStopped = StopAndWaitService(scm.Get(), 25000);
        if (serviceStopped) {
            UniqueScHandle svc{OpenServiceW(scm.Get(), kServiceName, DELETE)};
            if (svc.Valid()) {
                DeleteService(svc.Get());
            }
        }
    }

    if (!serviceStopped) {
        RegistryStore::SetUninstalling(false);
        WriteProgress(
            "error", 100,
            "AirpodsBridgeService could not stop cleanly. Restart Windows and disable again so "
            "the native Bluetooth driver can be restored safely.");
        ServiceLog::Shutdown();
        return 1;
    }

    WriteProgress("running", 65, "Restoring native Windows Bluetooth driver...");
    const bool restored = RadioDriverManager::RestoreAllBoundAdapters();
    RegistryStore::SetUninstalling(false);
    if (!restored) {
        WriteProgress(
            "error", 100,
            "Could not restore the native Windows Bluetooth driver while a device was busy. "
            "Restart Windows to finish restoring native Bluetooth.");
        ServiceLog::Shutdown();
        return 1;
    }

    RegistryStore::SetGuardTripped(false);
    WriteProgress(
        "ok", 100, "Control Engine disabled and native Windows Bluetooth driver restored.");
    ServiceLog::Shutdown();
    return 0;
}

int NativeSetup::RunRestoreNative()
{
    ServiceLog::Initialize(false);
    WriteProgress("running", 25, "Stopping service and restoring native Bluetooth driver...");

    bool serviceStopped = true;
    UniqueScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (scm.Valid()) {
        serviceStopped = StopAndWaitService(scm.Get(), 25000);
    }

    if (!serviceStopped) {
        WriteProgress(
            "error", 100,
            "AirpodsBridgeService could not stop cleanly. Restart Windows before restoring the "
            "native Bluetooth driver.");
        ServiceLog::Shutdown();
        return 1;
    }

    if (!RadioDriverManager::RestoreAllBoundAdapters()) {
        WriteProgress(
            "error", 100,
            "Could not restore the native Windows Bluetooth driver cleanly. Restart Windows and "
            "retry.");
        ServiceLog::Shutdown();
        return 1;
    }
    RegistryStore::SetGuardTripped(false);

    WriteProgress("ok", 100, "Native Windows Bluetooth driver restored.");
    ServiceLog::Shutdown();
    return 0;
}

} // namespace Service::Win
