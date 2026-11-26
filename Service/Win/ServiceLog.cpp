// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/ServiceLog.h"
#include "Service/Win/WinHandles.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace Service::Win {

namespace {

constexpr uintmax_t kMaxLogFileBytes = 5u * 1024u * 1024u;
constexpr wchar_t kLogDir[] = L"C:\\ProgramData\\AirpodsBridge";
constexpr wchar_t kLogPath[] = L"C:\\ProgramData\\AirpodsBridge\\RadioService.log";
constexpr wchar_t kLogBackupPath[] = L"C:\\ProgramData\\AirpodsBridge\\RadioService.1.log";

std::mutex gLogMutex;
std::ofstream gLogFile;
bool gMirrorConsole = false;

const char *SeverityTag(LogLevel s) noexcept
{
    switch (s) {
    case LogLevel::Trace:
        return "TRC";
    case LogLevel::Info:
        return "INF";
    case LogLevel::Warn:
        return "WRN";
    case LogLevel::Error:
        return "ERR";
    default:
        return "INF";
    }
}

void OpenOrRotateLocked()
{
    std::error_code ec;
    std::filesystem::create_directories(kLogDir, ec);
    if (std::filesystem::exists(kLogPath, ec) &&
        std::filesystem::file_size(kLogPath, ec) >= kMaxLogFileBytes)
    {
        if (gLogFile.is_open()) {
            gLogFile.close();
        }
        std::filesystem::remove(kLogBackupPath, ec);
        std::filesystem::rename(kLogPath, kLogBackupPath, ec);
    }
    if (!gLogFile.is_open()) {
        gLogFile.open(kLogPath, std::ios::out | std::ios::app | std::ios::binary);
    }
}

} // namespace

void ServiceLog::Initialize(bool mirrorConsole)
{
    std::lock_guard<std::mutex> lock{gLogMutex};
    gMirrorConsole = mirrorConsole;
    OpenOrRotateLocked();
}

void ServiceLog::Shutdown()
{
    std::lock_guard<std::mutex> lock{gLogMutex};
    if (gLogFile.is_open()) {
        gLogFile.flush();
        gLogFile.close();
    }
}

void ServiceLog::Write(LogLevel severity, std::string_view category, std::string_view message)
{
    SYSTEMTIME st{};
    GetLocalTime(&st);

    char prefix[96]{};
    std::snprintf(
        prefix, sizeof(prefix), "%04u-%02u-%02u %02u:%02u:%02u.%03u [%s] [%.*s] ", st.wYear,
        st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        SeverityTag(severity), static_cast<int>(category.size()), category.data());

    std::lock_guard<std::mutex> lock{gLogMutex};
    OpenOrRotateLocked();
    if (gLogFile.is_open()) {
        gLogFile.write(prefix, static_cast<std::streamsize>(std::strlen(prefix)));
        gLogFile.write(message.data(), static_cast<std::streamsize>(message.size()));
        gLogFile.put('\n');
        gLogFile.flush();
    }
    if (gMirrorConsole) {
        std::fwrite(prefix, 1, std::strlen(prefix), stdout);
        std::fwrite(message.data(), 1, message.size(), stdout);
        std::fputc('\n', stdout);
        std::fflush(stdout);
    }
}

void ServiceLog::ReportEventLogError(std::string_view message)
{
    HANDLE src = RegisterEventSourceW(nullptr, L"AirpodsBridgeService");
    if (src == nullptr) {
        return;
    }
    const std::wstring wmsg = Utf8ToWide(message);
    LPCWSTR strings[1] = {wmsg.c_str()};
    ReportEventW(src, EVENTLOG_ERROR_TYPE, 0, 1, nullptr, 1, 0, strings, nullptr);
    DeregisterEventSource(src);
}

} // namespace Service::Win
