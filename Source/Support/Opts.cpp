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
             value<bool>()->default_value("false"));

        //////////////////////////////////////////////////

        auto args = parser.parse(argc, argv);

        if (args.count("help")) {
            std::cout << parser.help() << std::endl;
            std::exit(0);
        }

        _opts.enableTrace = args["trace"].as<bool>();
        _opts.afterInstall = args["after-install"].as<bool>();

        return _opts;
    }
    catch (cxxopts::OptionException &exception) {
        FatalError(std::format("Parse options failed.\n\n{}", exception.what()), false);
        std::exit(1);
    }
}

} // namespace Opts
