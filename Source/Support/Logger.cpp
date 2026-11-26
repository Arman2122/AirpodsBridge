// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Support/Logger.h"

#include <chrono>

#include <QDir>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>

#include <Config.h>
#include "Source/Support/Helper.h"
#include "Source/Support/Utils.h"
#include "Source/Support/Error.h"

namespace Logger {
namespace {

// Cap log growth for long-running tray sessions while preserving the previous session's
// log across restarts and crashes.
constexpr size_t kMaxLogFileSizeBytes = 5 * 1024 * 1024;
constexpr size_t kMaxRotatedLogFiles = 2;

} // namespace

QDir GetLogFilePath()
{
    static std::optional<QDir> result;
    if (!result.has_value()) {
        const auto workspace = Utils::File::GetWorkspace();
        result = QDir{workspace.absoluteFilePath(CONFIG_PROGRAM_NAME ".log")};
    }
    return result.value();
}

bool Initialize(bool enableTrace)
{
#if defined APB_DEBUG
    enableTrace = true;
#endif

    try {
        const auto logFilePath = GetLogFilePath().absolutePath().toStdWString();

        // clang-format off
        auto logger = std::make_shared<spdlog::logger>(
            "Main", std::initializer_list<spdlog::sink_ptr>{
                std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                    logFilePath, kMaxLogFileSizeBytes, kMaxRotatedLogFiles),
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>()
            }
        );
        // clang-format on

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);

        spdlog::set_level(enableTrace ? spdlog::level::trace : spdlog::level::info);
        // Flush immediately on warnings/errors, and periodically otherwise so normal info
        // or trace logging does not force a synchronous disk flush on every packet.
        spdlog::flush_on(spdlog::level::warn);
        spdlog::flush_every(std::chrono::seconds{3});

#if defined APB_DEBUG
        spdlog::set_error_handler([](const std::string &msg) { Utils::Debug::BreakPoint(); });
#endif
        return true;
    }
    catch (spdlog::spdlog_ex &exception) {
        FatalError(std::format("spdlog initialize failed.\n\n{}", exception.what()), true);
        return false;
    }
}

} // namespace Logger
