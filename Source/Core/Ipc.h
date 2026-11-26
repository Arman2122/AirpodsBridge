// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include <QByteArray>

namespace Core::Ipc {

//
// Per-user local Named Pipe IPC for CLI automation (`AirpodsBridge.exe --status`,
// `--connect`, `--disconnect`, `--toggle-connect`, `--popup`, `--mode anc`, etc.).
//
// Zero network exposure (local `PIPE_REJECT_REMOTE_CLIENTS` per-user pipe name), zero
// polling (overlapped `ConnectNamedPipe` waiting on OS events), and zero extra Qt modules.
//
class Server
{
public:
    using Handler = std::function<QByteArray(const QByteArray &requestJson)>;

    explicit Server(Handler handler);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    bool Start();
    void Stop();

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

// Sends one JSON request to the running AirpodsBridge instance and returns its JSON reply.
// Returns `std::nullopt` if no instance is listening or the request times out.
std::optional<QByteArray> SendCommand(
    const QByteArray &requestJson,
    std::chrono::milliseconds timeout = std::chrono::milliseconds{1500});

std::wstring PipeNameForCurrentUser();

} // namespace Core::Ipc
