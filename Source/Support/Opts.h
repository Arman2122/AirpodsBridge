// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <format>
#include <optional>

#include <cxxopts.hpp>
#include <spdlog/fmt/ostr.h>

namespace Opts {

struct LaunchOpts {
    bool enableTrace{false};

    template <class OutStream>
    friend inline OutStream &operator<<(OutStream &outStream, const Opts::LaunchOpts &opts)
    {
        return outStream << std::format("{{ trace: {} }}", opts.enableTrace);
    }
};

class LaunchOptsManager
{
public:
    const LaunchOpts &Parse(int argc, char *argv[]);

    inline const auto &GetOpts() const
    {
        return _opts;
    }

private:
    LaunchOpts _opts;
};

} // namespace Opts
