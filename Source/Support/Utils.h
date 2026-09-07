// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <mutex>
#include <format>
#include <vector>
#include <cwctype>

#include <QDir>
#include <QStandardPaths>

#include "Source/Support/Helper.h"
#include "Source/Support/Logger.h"
#include "Source/Support/Error.h"

#include <Config.h>

#if defined APB_OS_WIN
    #include "Source/Support/OS/Windows.h"
#endif

namespace Utils {

namespace Debug {

inline void BreakPoint()
{
#if defined APB_DEBUG
    #if !defined APB_MSVC
        #error "Need to port."
    #endif

    #if defined APB_MSVC
    __debugbreak();
    #endif
#else
    LOG(Warn, "Triggered a break point.");
#endif
}
} // namespace Debug

namespace Text {

[[nodiscard]] constexpr std::string ToLower(std::string source)
{
    std::transform(source.begin(), source.end(), source.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return source;
}

[[nodiscard]] constexpr std::wstring ToLower(std::wstring source)
{
    std::transform(source.begin(), source.end(), source.begin(), &std::towlower);
    return source;
}

[[nodiscard]] constexpr std::string ToUpper(std::string source)
{
    std::transform(source.begin(), source.end(), source.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return source;
}

[[nodiscard]] constexpr std::wstring ToUpper(std::wstring source)
{
    std::transform(source.begin(), source.end(), source.begin(), &std::towupper);
    return source;
}

} // namespace Text

namespace File {

inline QDir GetWorkspace()
{
    auto location = QStandardPaths::writableLocation(QStandardPaths::DataLocation);

    QDir result{std::move(location)};
    if (!result.exists()) {
        result.mkpath(".");
    }
    return result;
}

inline bool OpenFileLocation(const QDir &directory)
{
#if defined APB_OS_WIN
    return Support::OS::Windows::File::OpenFileLocation(directory);
#else
    #error "Need to port."
#endif
}
} // namespace File

namespace Process {
//
// Retained for backward compatibility with v0.2.0 and before.
// TODO: Remove this function in [v1.0.0]
//
inline bool SingleInstance(const QString &instanceName)
{
#if !defined APB_OS_WIN
    #error "Need to port."
#endif
    HANDLE mutex = CreateMutexW(
        nullptr, false, ("Global\\" + instanceName + "_InstanceMutex").toStdWString().c_str());
    uint32_t lastError = GetLastError();

    if (mutex == nullptr) {
        FatalError(std::format("Create instance mutex failed.\nErrorCode: {}", lastError), false);
    }

    // No need to close the handle
    //
    return lastError != ERROR_ALREADY_EXISTS;
}

inline void AttachConsole()
{
#if defined APB_OS_WIN
    Support::OS::Windows::Process::AttachConsole();
#else
    #error "Need to port."
#endif
}

} // namespace Process
} // namespace Utils
