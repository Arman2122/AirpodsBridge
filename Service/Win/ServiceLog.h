// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <string_view>

namespace Service::Win {

enum class LogLevel : uint8_t {
    Trace = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
};

class ServiceLog
{
public:
    static void Initialize(bool mirrorConsole);
    static void Shutdown();
    static void Write(LogLevel severity, std::string_view category, std::string_view message);
    static void ReportEventLogError(std::string_view message);
};

} // namespace Service::Win
