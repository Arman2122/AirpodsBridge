// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/App/Application.h"

#include <Config.h>
#include "Source/Support/Utils.h"

int main(int argc, char *argv[])
{
    Utils::Process::AttachConsole();

    if (!Utils::Process::SingleInstance(Config::ProgramName)) {
        return 0;
    }

    ApbApplication::PreConstruction();
    ApbApplication app{argc, argv};
    if (!ApbApp->Prepare(argc, argv)) {
        return 1;
    }
    return ApbApp->Run();
}
