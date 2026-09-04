// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <mutex>
#include <vector>
#include <optional>

namespace Core::Debug {

struct AdvOverrideFields {
    bool enabled{false};
    std::vector<std::vector<uint8_t>> advs;
};

struct DebugConfigFields {
    AdvOverrideFields advOverride;
};

class DebugConfig
{
public:
    static DebugConfig &GetInstance();

    DebugConfig() = default;

    void UpdateAdvOverride(bool enabled, std::vector<std::vector<uint8_t>> advs);
    std::optional<std::vector<uint8_t>> GetOverrideAdv() const;

private:
    mutable std::mutex _mutex;
    AdvOverrideFields _fields;
};

} // namespace Core::Debug
