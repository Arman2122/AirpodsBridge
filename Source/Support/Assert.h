// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <source_location>

#include "Source/Support/Helper.h"

#define APB_ASSERT(condition)                                                                      \
    while (!(condition)) {                                                                         \
        Assert::Trigger(TO_STRING(condition));                                                     \
    }

namespace Assert {

void Trigger(
    const std::string &condition,
    const std::source_location &srcloc = std::source_location::current());

} // namespace Assert
