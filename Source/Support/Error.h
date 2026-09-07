// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <string>

namespace Error {

void Initialize();

} // namespace Error

[[noreturn]] void FatalError(const std::string &content, bool report);
