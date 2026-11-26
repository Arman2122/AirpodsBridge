// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <format>
#include <optional>
#include <string>

#include <cxxopts.hpp>
#include <spdlog/fmt/ostr.h>

namespace Opts {

struct LaunchOpts {
    bool enableTrace{false};
    // The installer passes `--after-install` when it launches the program from its finish
    // page, so that the program can say where it went.
    bool afterInstall{false};

    // CLI automation commands forwarded to the running tray instance via per-user IPC.
    bool cliStatus{false};
    bool cliConnect{false};
    bool cliDisconnect{false};
    bool cliToggleConnect{false};
    bool cliPopup{false};
    bool cliSettings{false};
    std::string cliNoiseMode;

    // Native elevated helper task (invoked via UAC `runas` on our own executable — zero
    // PowerShell or external script dependencies across x86, x64, and ARM64 Windows).
    std::string applyLatencyProfile;
    std::string latencyEndpointGuid;
    std::string latencyContainerId;

    bool HasCliCommand() const
    {
        return cliStatus || cliConnect || cliDisconnect || cliToggleConnect || cliPopup ||
               cliSettings || !cliNoiseMode.empty();
    }

    bool HasElevatedTask() const
    {
        return !applyLatencyProfile.empty();
    }

    template <class OutStream>
    friend inline OutStream &operator<<(OutStream &outStream, const Opts::LaunchOpts &opts)
    {
        return outStream << std::format(
                   "{{ trace: {}, after-install: {}, cli: {} }}", opts.enableTrace,
                   opts.afterInstall, opts.HasCliCommand());
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
