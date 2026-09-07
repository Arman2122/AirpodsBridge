// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/Debug.h"

#include <random>

namespace Core::Debug {

DebugConfig &DebugConfig::GetInstance()
{
    static DebugConfig i;
    return i;
}

void DebugConfig::UpdateAdvOverride(bool enabled, std::vector<std::vector<uint8_t>> advs)
{
    std::lock_guard<std::mutex> lock{_mutex};

    _fields.enabled = enabled;
    _fields.advs = std::move(advs);
}

std::optional<std::vector<uint8_t>> DebugConfig::GetOverrideAdv() const
{
    std::optional<std::vector<uint8_t>> result;

    std::lock_guard<std::mutex> lock{_mutex};

    do {
        if (!_fields.enabled || _fields.advs.empty()) {
            break;
        }

        std::default_random_engine re{std::random_device{}()};
        std::uniform_int_distribution<size_t> dist{0, _fields.advs.size() - 1};

        result = _fields.advs.at(dist(re));
    } while (false);

    return result;
}

} // namespace Core::Debug
