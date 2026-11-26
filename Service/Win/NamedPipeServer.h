// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

#include "Service/Protocol/ServiceProtocol.h"
#include "Service/Win/WinHandles.h"

namespace Service::Win {

//
// Local-only multi-client Named Pipe server (`\\.\pipe\AirpodsBridge.RadioService.v1`).
//
// Enforces `PIPE_REJECT_REMOTE_CLIENTS` and an explicit SDDL DACL allowing LocalSystem,
// Builtin Administrators, and local Authenticated Users (so the non-elevated AirpodsBridge
// tray app can talk cleanly to the x64 Windows service without requiring UAC on every launch).
//
class NamedPipeServer
{
public:
    using FrameHandler = std::function<void(uint64_t clientId, const Protocol::Frame &frame)>;

    NamedPipeServer() = default;
    ~NamedPipeServer();

    NamedPipeServer(const NamedPipeServer &) = delete;
    NamedPipeServer &operator=(const NamedPipeServer &) = delete;

    bool Start(FrameHandler onFrame);
    void Stop();

    void
    SendToClient(uint64_t clientId, Protocol::MessageType type, std::span<const uint8_t> payload);
    void Broadcast(Protocol::MessageType type, std::span<const uint8_t> payload);

private:
    struct ClientSession {
        uint64_t id{0};
        UniqueHandle pipe;
        std::mutex writeMutex;
        std::atomic<bool> closed{false};
        std::thread readerThread;
    };

    void AcceptLoop();
    void ClientReaderLoop(std::shared_ptr<ClientSession> session);
    static bool WriteFrameLocked(HANDLE pipe, std::span<const uint8_t> encoded);

    std::atomic<bool> _running{false};
    UniqueHandle _stopEvent;
    std::thread _acceptThread;
    FrameHandler _onFrame;

    std::atomic<uint64_t> _nextClientId{1};
    std::mutex _clientsMutex;
    std::vector<std::shared_ptr<ClientSession>> _clients;
};

} // namespace Service::Win
