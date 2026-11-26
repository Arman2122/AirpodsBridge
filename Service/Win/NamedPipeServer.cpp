// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/NamedPipeServer.h"
#include "Service/Win/ServiceLog.h"

#include <sddl.h>

#include <algorithm>
#include <array>

namespace Service::Win {

namespace {

// LocalSystem (FA), Administrators (FA), Authenticated Users (RW).
constexpr wchar_t kPipeSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;AU)";

HANDLE CreatePipeInstance()
{
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            kPipeSddl, SDDL_REVISION_1, &sd, nullptr))
    {
        return INVALID_HANDLE_VALUE;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    HANDLE h = CreateNamedPipeW(
        Protocol::kPipeNameW, PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        PIPE_UNLIMITED_INSTANCES, 64 * 1024, 64 * 1024, 0, &sa);
    LocalFree(sd);
    return h;
}

} // namespace

NamedPipeServer::~NamedPipeServer()
{
    Stop();
}

bool NamedPipeServer::Start(FrameHandler onFrame)
{
    Stop();
    _onFrame = std::move(onFrame);
    _stopEvent = UniqueHandle{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!_stopEvent.Valid()) {
        return false;
    }
    _running.store(true);
    _acceptThread = std::thread{&NamedPipeServer::AcceptLoop, this};
    ServiceLog::Write(LogLevel::Info, "pipe", "Named Pipe server started.");
    return true;
}

void NamedPipeServer::Stop()
{
    if (!_running.exchange(false)) {
        return;
    }
    if (_stopEvent.Valid()) {
        SetEvent(_stopEvent.Get());
    }
    if (_acceptThread.joinable()) {
        _acceptThread.join();
    }

    std::vector<std::shared_ptr<ClientSession>> clients;
    {
        std::lock_guard<std::mutex> lock{_clientsMutex};
        clients = std::move(_clients);
        _clients.clear();
    }
    for (auto &c : clients) {
        c->closed.store(true);
        if (c->pipe.Valid()) {
            CancelIoEx(c->pipe.Get(), nullptr);
            DisconnectNamedPipe(c->pipe.Get());
        }
        if (c->readerThread.joinable() && c->readerThread.get_id() != std::this_thread::get_id()) {
            c->readerThread.join();
        }
    }
    _stopEvent.Reset();
}

void NamedPipeServer::SendToClient(
    uint64_t clientId, Protocol::MessageType type, std::span<const uint8_t> payload)
{
    const auto encoded = Protocol::EncodeFrame(type, payload);
    std::shared_ptr<ClientSession> target;
    {
        std::lock_guard<std::mutex> lock{_clientsMutex};
        for (const auto &c : _clients) {
            if (c->id == clientId) {
                target = c;
                break;
            }
        }
    }
    if (!target || target->closed.load()) {
        return;
    }
    std::lock_guard<std::mutex> wlock{target->writeMutex};
    if (!target->closed.load() && target->pipe.Valid()) {
        WriteFrameLocked(target->pipe.Get(), encoded);
    }
}

void NamedPipeServer::Broadcast(Protocol::MessageType type, std::span<const uint8_t> payload)
{
    const auto encoded = Protocol::EncodeFrame(type, payload);
    std::vector<std::shared_ptr<ClientSession>> snapshot;
    {
        std::lock_guard<std::mutex> lock{_clientsMutex};
        snapshot = _clients;
    }
    for (const auto &c : snapshot) {
        if (c->closed.load()) {
            continue;
        }
        std::lock_guard<std::mutex> wlock{c->writeMutex};
        if (!c->closed.load() && c->pipe.Valid()) {
            WriteFrameLocked(c->pipe.Get(), encoded);
        }
    }
}

void NamedPipeServer::AcceptLoop()
{
    UniqueHandle connectEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    while (_running.load()) {
        UniqueHandle pipe{CreatePipeInstance()};
        if (!pipe.Valid()) {
            ServiceLog::Write(
                LogLevel::Error, "pipe",
                "CreateNamedPipeW failed: " + FormatWin32Error(GetLastError()));
            Sleep(500);
            continue;
        }

        ResetEvent(connectEvent.Get());
        OVERLAPPED ov{};
        ov.hEvent = connectEvent.Get();

        BOOL ok = ConnectNamedPipe(pipe.Get(), &ov);
        if (!ok) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {_stopEvent.Get(), connectEvent.Get()};
                const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(pipe.Get(), &ov);
                    break;
                }
                DWORD ignored = 0;
                ok = GetOverlappedResult(pipe.Get(), &ov, &ignored, FALSE);
            }
            else if (err == ERROR_PIPE_CONNECTED) {
                ok = TRUE;
            }
        }

        if (!ok || !_running.load()) {
            continue;
        }

        auto session = std::make_shared<ClientSession>();
        session->id = _nextClientId.fetch_add(1);
        session->pipe = std::move(pipe);
        {
            std::lock_guard<std::mutex> lock{_clientsMutex};
            _clients.push_back(session);
        }
        session->readerThread = std::thread{&NamedPipeServer::ClientReaderLoop, this, session};
    }
}

void NamedPipeServer::ClientReaderLoop(std::shared_ptr<ClientSession> session)
{
    UniqueHandle readEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    std::array<uint8_t, 8192> chunk{};
    std::vector<uint8_t> stream;
    stream.reserve(16384);

    while (_running.load() && !session->closed.load()) {
        ResetEvent(readEvent.Get());
        OVERLAPPED ov{};
        ov.hEvent = readEvent.Get();

        DWORD got = 0;
        BOOL ok = ReadFile(
            session->pipe.Get(), chunk.data(), static_cast<DWORD>(chunk.size()), &got, &ov);
        if (!ok) {
            const DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                HANDLE waits[2] = {_stopEvent.Get(), readEvent.Get()};
                const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                if (wait != WAIT_OBJECT_0 + 1) {
                    CancelIoEx(session->pipe.Get(), &ov);
                    break;
                }
                ok = GetOverlappedResult(session->pipe.Get(), &ov, &got, FALSE);
            }
            if (!ok) {
                break;
            }
        }

        if (got == 0) {
            break;
        }

        stream.insert(stream.end(), chunk.begin(), chunk.begin() + got);

        for (;;) {
            Protocol::Frame frame{};
            size_t consumed = 0;
            const auto st = Protocol::TryDecodeFrame(stream, frame, consumed);
            if (st == Protocol::DecodeStatus::NeedMoreData) {
                break;
            }
            if (st == Protocol::DecodeStatus::InvalidFrame) {
                session->closed.store(true);
                break;
            }
            stream.erase(stream.begin(), stream.begin() + static_cast<std::ptrdiff_t>(consumed));
            if (_onFrame) {
                _onFrame(session->id, frame);
            }
        }
    }

    session->closed.store(true);
    {
        std::lock_guard<std::mutex> lock{_clientsMutex};
        _clients.erase(
            std::remove_if(
                _clients.begin(), _clients.end(),
                [id = session->id](const auto &c) { return c->id == id; }),
            _clients.end());
    }
    if (session->readerThread.joinable() &&
        session->readerThread.get_id() == std::this_thread::get_id())
    {
        session->readerThread.detach();
    }
}

bool NamedPipeServer::WriteFrameLocked(HANDLE pipe, std::span<const uint8_t> encoded)
{
    UniqueHandle ev{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    if (!ev.Valid()) {
        return false;
    }
    OVERLAPPED ov{};
    ov.hEvent = ev.Get();
    DWORD written = 0;
    BOOL ok = WriteFile(pipe, encoded.data(), static_cast<DWORD>(encoded.size()), &written, &ov);
    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        if (WaitForSingleObject(ev.Get(), 2000) == WAIT_OBJECT_0) {
            ok = GetOverlappedResult(pipe, &ov, &written, FALSE);
        }
        else {
            CancelIoEx(pipe, &ov);
            return false;
        }
    }
    return ok != FALSE && written == encoded.size();
}

} // namespace Service::Win
