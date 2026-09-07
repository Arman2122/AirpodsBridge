// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Support/Assert.h"

#include <format>

#include "Source/Support/Error.h"

namespace Assert {

void Trigger(const std::string &condition, const std::source_location &srcloc)
{
    auto content = std::format(
        "Assertion triggered.\n"
        "\n"
        "Condition: {}\n"
        "File: {}\n"
        "Line: {}",
        condition, srcloc.file_name(), srcloc.line());

    FatalError(content, true);
}
} // namespace Assert
