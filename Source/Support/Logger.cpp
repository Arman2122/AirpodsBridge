// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Support/Logger.h"

#include <QDir>
#include <spdlog/sinks/sink.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/pattern_formatter.h>

#include <Config.h>
#include "Source/Support/Helper.h"
#include "Source/Support/Utils.h"
#include "Source/Support/Error.h"

namespace Logger {

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
                std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFilePath, true),
                std::make_shared<spdlog::sinks::stdout_color_sink_mt>()
            }
        );
        // clang-format on

        spdlog::register_logger(logger);
        spdlog::set_default_logger(logger);

        spdlog::set_level(enableTrace ? spdlog::level::trace : spdlog::level::info);
        spdlog::flush_on(spdlog::level::trace);

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

// TODO: Remove this function in [v1.0.0]
void CleanUpOldLogFiles()
{
    auto workspace = Utils::File::GetWorkspace();

    for (size_t i = 1; i < 10000; ++i) {
        auto logFile = QString{CONFIG_PROGRAM_NAME ".%1.log"}.arg(i);

        if (!workspace.exists(logFile)) {
            LOG(Info, "Clean up old log file: '{}' doesn't exist, break the loop", logFile);
            break;
        }

        LOG(Info, "Clean up old log file: '{}' exists, remove it", logFile);
        workspace.remove(logFile);
    }
}
} // namespace Logger
