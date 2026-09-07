// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Support/Error.h"

#include <format>
#include <cstdlib>
#include <fstream>

#include <QDesktopServices>
#include <QUrl>

#include <boost/stacktrace.hpp>

#include <Config.h>
#include "Source/Support/Utils.h"

constexpr auto kStackTraceFileName = "StackTrace.log";

namespace Error {
namespace Impl {

void WriteStackTraceFile()
{
    using namespace boost;

    auto workspace = Utils::File::GetWorkspace();
    std::ofstream file{workspace.absoluteFilePath(kStackTraceFileName).toStdString()};

    file << stacktrace::stacktrace();
}
} // namespace Impl

void Initialize()
{
    auto workspace = Utils::File::GetWorkspace();

    // Delete the last StackTrace log file, if any
    //
    workspace.remove(kStackTraceFileName);
}
} // namespace Error

[[noreturn]] void FatalError(const std::string &content, bool report)
{
    Error::Impl::WriteStackTraceFile();

#if !defined APB_OS_WIN
    #error "Need to port."
#endif

    // Because QMessageBox does not allow calls from non-GUI threads.
    // So far, there seems to be no better way than using native APIs.
    //

    auto title = std::format("{} fatal error", Config::ProgramName);

    if (report) {
        auto message = std::format(
            "An error has occurred!\n"
            "Please help us fix this problem.\n"
            "--------------------------------------------------\n"
            "\n"
            "{}\n"
            "\n"
            "--------------------------------------------------\n"
            "Click \"Yes\" will pop up GitHub issue tracker page.\n"
            "You can submit this information to us there.\n"
            "Thank you very much.",
            content);

        int button = MessageBoxA(nullptr, message.c_str(), title.c_str(), MB_ICONERROR | MB_YESNO);

#if !defined APB_DEBUG
        if (button == IDYES) {
            QDesktopServices::openUrl(QUrl{Config::UrlIssues});
        }
#endif
    }
    else {
        MessageBoxA(nullptr, content.c_str(), title.c_str(), MB_ICONERROR | MB_OK);
    }

#if defined APB_DEBUG
    Utils::Debug::BreakPoint();
#endif

    std::abort();
}
