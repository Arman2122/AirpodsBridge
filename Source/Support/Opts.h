// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <format>
#include <optional>

#include <cxxopts.hpp>
#include <spdlog/fmt/ostr.h>

namespace Opts {

struct LaunchOpts {
    bool enableTrace{false};
    // The installer passes `--after-install` when it launches the program from its finish
    // page, so that the program can say where it went.
    bool afterInstall{false};

    template <class OutStream>
    friend inline OutStream &operator<<(OutStream &outStream, const Opts::LaunchOpts &opts)
    {
        return outStream << std::format(
                   "{{ trace: {}, after-install: {} }}", opts.enableTrace, opts.afterInstall);
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
