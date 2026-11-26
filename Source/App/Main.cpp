// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <iostream>

#include <nlohmann/json.hpp>

#include "Source/App/Application.h"
#include "Source/Core/AudioControl.h"
#include "Source/Core/Ipc.h"
#include "Source/Support/Opts.h"
#include "Source/Support/Utils.h"

namespace {

int RunCliCommand(const Opts::LaunchOpts &opts)
{
    nlohmann::json req;
    if (opts.cliStatus) {
        req["cmd"] = "status";
    }
    else if (opts.cliConnect) {
        req["cmd"] = "connect";
    }
    else if (opts.cliDisconnect) {
        req["cmd"] = "disconnect";
    }
    else if (opts.cliToggleConnect) {
        req["cmd"] = "toggle";
    }
    else if (opts.cliPopup) {
        req["cmd"] = "popup";
    }
    else if (opts.cliSettings) {
        req["cmd"] = "settings";
    }
    else if (!opts.cliNoiseMode.empty()) {
        req["cmd"] = "mode";
        req["value"] = opts.cliNoiseMode;
    }

    const auto payload = QByteArray::fromStdString(req.dump());
    const auto reply = Core::Ipc::SendCommand(payload);
    if (!reply.has_value()) {
        std::cerr << "{\"ok\":false,\"error\":\"AirpodsBridge is not running\"}\n";
        return 1;
    }

    std::cout << reply->toStdString() << "\n";
    try {
        const auto parsed = nlohmann::json::parse(reply->toStdString());
        return parsed.value("ok", false) ? 0 : 1;
    }
    catch (...) {
        return 0;
    }
}

} // namespace

int main(int argc, char *argv[])
{
    Utils::Process::AttachConsole();

    Opts::LaunchOptsManager preParser;
    const auto &preOpts = preParser.Parse(argc, argv);
    if (preOpts.HasElevatedTask()) {
        const auto &mode = preOpts.applyLatencyProfile;
        const bool lowLatency = (mode == "sbc" || mode == "low-latency" || mode == "LowLatency");
        const auto result = Core::AudioControl::ApplyLatencyProfileInProcess(
            lowLatency, QString::fromStdString(preOpts.latencyEndpointGuid),
            QString::fromStdString(preOpts.latencyContainerId));
        if (!result.succeeded) {
            return 1;
        }
        return result.a2dpRenegotiated ? 0 : 2;
    }
    if (preOpts.HasCliCommand()) {
        return RunCliCommand(preOpts);
    }

    ApbApplication::PreConstruction();
    ApbApplication app{argc, argv};
    if (!ApbApp->Prepare(argc, argv)) {
        return 1;
    }
    return ApbApp->Run();
}
