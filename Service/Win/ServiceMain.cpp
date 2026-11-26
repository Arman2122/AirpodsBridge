// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <cstdio>
#include <iostream>
#include <string>

#include "Service/Protocol/ServiceProtocol.h"
#include "Service/Win/NativeSetup.h"
#include "Service/Win/RadioDriverManager.h"
#include "Service/Win/RadioEngine.h"
#include "Service/Win/RegistryStore.h"
#include "Service/Win/ServiceLog.h"
#include "Service/Win/WinHandles.h"

namespace {

constexpr wchar_t kServiceName[] = L"AirpodsBridgeService";
constexpr wchar_t kServiceDisplayName[] = L"AirpodsBridge Radio Proxy Service";
constexpr wchar_t kServiceDescription[] =
    L"Provides two-way Apple Accessory Protocol (AAP) control and stereo-safe AirPods audio "
    L"without Test Mode using inbox WinUSB and signed usbip-win2.";

SERVICE_STATUS_HANDLE gStatusHandle = nullptr;
SERVICE_STATUS gServiceStatus{};
Service::Win::RadioEngine *gEngine = nullptr;
HANDLE gStopEvent = nullptr;

void ReportServiceState(DWORD state, DWORD exitCode = NO_ERROR, DWORD waitHint = 0)
{
    if (gStatusHandle == nullptr) {
        return;
    }
    gServiceStatus.dwCurrentState = state;
    gServiceStatus.dwWin32ExitCode = exitCode;
    gServiceStatus.dwWaitHint = waitHint;
    gServiceStatus.dwControlsAccepted =
        (state == SERVICE_RUNNING)
            ? (SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN | SERVICE_ACCEPT_POWEREVENT)
            : 0;
    SetServiceStatus(gStatusHandle, &gServiceStatus);
}

DWORD WINAPI
ServiceCtrlHandlerEx(DWORD control, DWORD eventType, LPVOID /*eventData*/, LPVOID /*ctx*/)
{
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        ReportServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
        if (gStopEvent != nullptr) {
            SetEvent(gStopEvent);
        }
        return NO_ERROR;

    case SERVICE_CONTROL_POWEREVENT:
        if (gEngine != nullptr) {
            if (eventType == PBT_APMSUSPEND) {
                gEngine->OnSystemSuspend();
            }
            else if (eventType == PBT_APMRESUMEAUTOMATIC || eventType == PBT_APMRESUMESUSPEND) {
                gEngine->OnSystemResume();
            }
        }
        return NO_ERROR;

    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;

    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

void WINAPI ServiceMainEntry(DWORD /*argc*/, LPWSTR * /*argv*/)
{
    Service::Win::ServiceLog::Initialize(false);
    gServiceStatus.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    gStatusHandle = RegisterServiceCtrlHandlerExW(kServiceName, ServiceCtrlHandlerEx, nullptr);
    if (gStatusHandle == nullptr) {
        return;
    }

    ReportServiceState(SERVICE_START_PENDING, NO_ERROR, 5000);
    gStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    Service::Win::RadioEngine engine;
    gEngine = &engine;
    engine.Start();
    ReportServiceState(SERVICE_RUNNING);

    if (gStopEvent != nullptr) {
        WaitForSingleObject(gStopEvent, INFINITE);
    }

    ReportServiceState(SERVICE_STOP_PENDING, NO_ERROR, 10000);
    engine.Stop();
    gEngine = nullptr;
    if (gStopEvent != nullptr) {
        CloseHandle(gStopEvent);
        gStopEvent = nullptr;
    }

    Service::Win::ServiceLog::Shutdown();
    ReportServiceState(SERVICE_STOPPED);
}

int InstallService()
{
    wchar_t exePath[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) {
        std::fprintf(stderr, "Failed to resolve service executable path.\n");
        return 1;
    }

    Service::Win::UniqueScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE)};
    if (!scm.Valid()) {
        std::fprintf(
            stderr, "OpenSCManagerW failed (run as Administrator): %s\n",
            Service::Win::FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    const std::wstring binPath = L"\"" + std::wstring{exePath} + L"\"";
    Service::Win::UniqueScHandle svc{OpenServiceW(scm.Get(), kServiceName, SERVICE_ALL_ACCESS)};
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
        std::fprintf(
            stderr, "CreateServiceW failed: %s\n",
            Service::Win::FormatWin32Error(GetLastError()).c_str());
        return 1;
    }

    SERVICE_DESCRIPTIONW desc{};
    desc.lpDescription = const_cast<LPWSTR>(kServiceDescription);
    ChangeServiceConfig2W(svc.Get(), SERVICE_CONFIG_DESCRIPTION, &desc);

    // Automatic SCM recovery: restart up to 2 times with delay, then stop (watchdog handles
    // rollback).
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

    Service::Win::RegistryStore::SetUninstalling(false);
    Service::Win::RegistryStore::SetGuardTripped(false);
    std::printf("Installed %ls successfully.\n", kServiceName);
    return 0;
}

int UninstallService()
{
    Service::Win::RegistryStore::SetUninstalling(true);

    Service::Win::UniqueScHandle scm{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (scm.Valid()) {
        Service::Win::UniqueScHandle svc{
            OpenServiceW(scm.Get(), kServiceName, SERVICE_STOP | SERVICE_QUERY_STATUS | DELETE)};
        if (svc.Valid()) {
            SERVICE_STATUS st{};
            ControlService(svc.Get(), SERVICE_CONTROL_STOP, &st);
            for (int i = 0; i < 40; ++i) {
                if (!QueryServiceStatus(svc.Get(), &st) || st.dwCurrentState == SERVICE_STOPPED) {
                    break;
                }
                Sleep(250);
            }
            DeleteService(svc.Get());
        }
    }

    Service::Win::RadioDriverManager::RestoreAllBoundAdapters();
    Service::Win::RegistryStore::SetGuardTripped(false);
    Service::Win::RegistryStore::SetUninstalling(false);
    std::printf("Uninstalled %ls and restored native Bluetooth driver.\n", kServiceName);
    return 0;
}

int PrintPreflightJson()
{
    const bool usbipReady = Service::Win::RadioDriverManager::IsUsbipDriverReady();
    const auto search = Service::Win::RadioDriverManager::FindPhysicalAdapter();
    const bool guard = Service::Win::RegistryStore::GetGuardTripped();

    std::printf("{\n");
    std::printf("  \"usbipDriverReady\": %s,\n", usbipReady ? "true" : "false");
    std::printf("  \"guardTripped\": %s,\n", guard ? "true" : "false");
    std::printf(
        "  \"adapterStatus\": \"%s\",\n",
        search.status == Service::Win::AdapterSearchStatus::Found
            ? "found"
            : (search.status == Service::Win::AdapterSearchStatus::NonUsbOnly ? "non_usb_only"
                                                                              : "none"));
    if (search.adapter.has_value()) {
        std::printf("  \"adapterName\": \"%s\",\n", search.adapter->displayName.c_str());
        std::printf("  \"vid\": \"0x%04X\",\n", search.adapter->vid);
        std::printf("  \"pid\": \"0x%04X\",\n", search.adapter->pid);
        std::printf(
            "  \"compositeFunction\": %s,\n",
            search.adapter->isCompositeFunction ? "true" : "false");
        std::printf("  \"onWinUsb\": %s\n", search.adapter->onWinUsb ? "true" : "false");
    }
    else {
        std::printf("  \"adapterName\": \"\"\n");
    }
    std::printf("}\n");
    return 0;
}

int RunConsoleMode()
{
    Service::Win::ServiceLog::Initialize(true);
    std::printf("Running AirpodsBridgeService in interactive console mode. Press Enter to stop.\n");
    Service::Win::RadioEngine engine;
    engine.Start();
    std::string line;
    std::getline(std::cin, line);
    engine.Stop();
    Service::Win::ServiceLog::Shutdown();
    return 0;
}

} // namespace

int wmain(int argc, wchar_t *argv[])
{
    if (argc >= 2) {
        const std::wstring arg{argv[1]};
        if (arg == L"--setup-enable") {
            return Service::Win::NativeSetup::RunEnable();
        }
        if (arg == L"--setup-disable") {
            return Service::Win::NativeSetup::RunDisable();
        }
        if (arg == L"--setup-restore") {
            return Service::Win::NativeSetup::RunRestoreNative();
        }
        if (arg == L"--install") {
            return InstallService();
        }
        if (arg == L"--uninstall") {
            return UninstallService();
        }
        if (arg == L"--restore-radio") {
            Service::Win::ServiceLog::Initialize(true);
            Service::Win::RadioDriverManager::RestoreAllBoundAdapters();
            Service::Win::RegistryStore::SetGuardTripped(false);
            Service::Win::ServiceLog::Shutdown();
            std::printf("Native Bluetooth driver restored.\n");
            return 0;
        }
        if (arg == L"--preflight" || arg == L"--status") {
            return PrintPreflightJson();
        }
        if (arg == L"--console") {
            return RunConsoleMode();
        }
        std::printf("AirpodsBridgeService (x64 No-Test-Mode Bluetooth Radio Proxy)\n"
                    "Usage:\n"
                    "  --install        Register Windows service (requires Admin)\n"
                    "  --uninstall      Stop service, restore native BT driver, unregister\n"
                    "  --restore-radio  Emergency native Bluetooth driver restore\n"
                    "  --preflight      Inspect usbip-win2 & USB Bluetooth adapter readiness\n"
                    "  --console        Run interactively in console for debugging\n");
        return 0;
    }

    SERVICE_TABLE_ENTRYW dispatchTable[] = {
        {const_cast<LPWSTR>(kServiceName), ServiceMainEntry},
        {nullptr, nullptr},
    };
    if (!StartServiceCtrlDispatcherW(dispatchTable)) {
        const DWORD err = GetLastError();
        if (err == ERROR_FAILED_SERVICE_CONTROLLER_CONNECT) {
            return PrintPreflightJson();
        }
        return 1;
    }
    return 0;
}
