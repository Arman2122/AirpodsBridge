// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Support/Opts.h"

#include <format>

#include <Config.h>
#include "Source/Support/Error.h"

namespace Opts {

using namespace cxxopts;

const LaunchOpts &LaunchOptsManager::Parse(int argc, char *argv[])
{
    try {
        Options parser{Config::ProgramName, Config::Description};

        parser.add_options()                                                                //
            ("help", "Print options")                                                       //
            ("trace", "Enable trace level logging.", value<bool>()->default_value("false")) //
            ("after-install",
             "Show a notification that the program is running in the system tray. The installer "
             "passes this when it launches the program.",
             value<bool>()->default_value("false")) //
            ("status", "Print live AirPods status as JSON from the running instance.",
             value<bool>()->default_value("false")) //
            ("connect", "Ask the running instance to connect the configured AirPods.",
             value<bool>()->default_value("false")) //
            ("disconnect", "Ask the running instance to disconnect the configured AirPods.",
             value<bool>()->default_value("false")) //
            ("toggle-connect", "Toggle connect/disconnect on the running instance.",
             value<bool>()->default_value("false")) //
            ("popup", "Toggle the battery popup window on the running instance.",
             value<bool>()->default_value("false")) //
            ("settings", "Open the Settings window on the running instance.",
             value<bool>()->default_value("false")) //
            ("mode", "Set Noise Control mode (off, anc, transparency, adaptive, cycle).",
             value<std::string>()->default_value("")) //
            ("apply-latency-profile",
             "Internal elevated helper: switch Windows BthA2dp latency profile (sbc or aac).",
             value<std::string>()->default_value("")) //
            ("endpoint-guid", "Optional WASAPI render endpoint GUID for --apply-latency-profile.",
             value<std::string>()->default_value("")) //
            ("container-id", "Optional Bluetooth device ContainerId for --apply-latency-profile.",
             value<std::string>()->default_value(""));

        //////////////////////////////////////////////////

        auto args = parser.parse(argc, argv);

        if (args.count("help")) {
            std::cout << parser.help() << std::endl;
            std::exit(0);
        }

        _opts.enableTrace = args["trace"].as<bool>();
        _opts.afterInstall = args["after-install"].as<bool>();
        _opts.cliStatus = args["status"].as<bool>();
        _opts.cliConnect = args["connect"].as<bool>();
        _opts.cliDisconnect = args["disconnect"].as<bool>();
        _opts.cliToggleConnect = args["toggle-connect"].as<bool>();
        _opts.cliPopup = args["popup"].as<bool>();
        _opts.cliSettings = args["settings"].as<bool>();
        _opts.cliNoiseMode = args["mode"].as<std::string>();
        _opts.applyLatencyProfile = args["apply-latency-profile"].as<std::string>();
        _opts.latencyEndpointGuid = args["endpoint-guid"].as<std::string>();
        _opts.latencyContainerId = args["container-id"].as<std::string>();

        return _opts;
    }
    catch (cxxopts::OptionException &exception) {
        FatalError(std::format("Parse options failed.\n\n{}", exception.what()), false);
        std::exit(1);
    }
}

} // namespace Opts
