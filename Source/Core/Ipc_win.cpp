// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/Ipc.h"

#include <array>
#include <atomic>
#include <thread>

#include <QCryptographicHash>
#include <QString>

#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"

namespace Core::Ipc {

namespace {

constexpr DWORD kMaxMessageBytes = 16 * 1024;

} // namespace

std::wstring PipeNameForCurrentUser()
{
    wchar_t userName[256]{};
    DWORD len = static_cast<DWORD>(std::size(userName));
    QString key = QStringLiteral("default");
    if (GetUserNameW(userName, &len) != FALSE && len > 1) {
        key = QString::fromWCharArray(userName, static_cast<int>(len - 1)).toLower();
    }
    const QByteArray hash =
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha256).toHex().left(16);
    return (QStringLiteral("\\\\.\\pipe\\AirpodsBridge.IPC.") + QString::fromLatin1(hash))
        .toStdWString();
}

class Server::Impl final
{
public:
    explicit Impl(Handler handler)
        : _handler{std::move(handler)}, _pipeName{PipeNameForCurrentUser()}
    {
    }

    ~Impl()
    {
        Stop();
    }

    bool Start()
    {
        if (_running.exchange(true)) {
            return true;
        }
        _stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        _readyEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (_stopEvent == nullptr || _readyEvent == nullptr) {
            if (_stopEvent != nullptr) {
                CloseHandle(_stopEvent);
                _stopEvent = nullptr;
            }
            if (_readyEvent != nullptr) {
                CloseHandle(_readyEvent);
                _readyEvent = nullptr;
            }
            _running.store(false);
            return false;
        }
        _thread = std::thread{&Impl::Loop, this};
        WaitForSingleObject(_readyEvent, 2000);
        return true;
    }

    void Stop()
    {
        if (!_running.exchange(false)) {
            return;
        }
        if (_stopEvent != nullptr) {
            SetEvent(_stopEvent);
        }
        if (_thread.joinable()) {
            _thread.join();
        }
        if (_stopEvent != nullptr) {
            CloseHandle(_stopEvent);
            _stopEvent = nullptr;
        }
        if (_readyEvent != nullptr) {
            CloseHandle(_readyEvent);
            _readyEvent = nullptr;
        }
    }

private:
    void Loop()
    {
        HANDLE ioEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (ioEvent == nullptr) {
            SetEvent(_readyEvent);
            return;
        }

        while (WaitForSingleObject(_stopEvent, 0) != WAIT_OBJECT_0) {
            HANDLE pipe = CreateNamedPipeW(
                _pipeName.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                4, kMaxMessageBytes, kMaxMessageBytes, 1000, nullptr);
            if (pipe == INVALID_HANDLE_VALUE) {
                SetEvent(_readyEvent);
                LOG(Warn, "IPC: CreateNamedPipeW failed (err={}).", GetLastError());
                if (WaitForSingleObject(_stopEvent, 1000) == WAIT_OBJECT_0) {
                    break;
                }
                continue;
            }

            SetEvent(_readyEvent);

            ResetEvent(ioEvent);
            OVERLAPPED ov{};
            ov.hEvent = ioEvent;

            BOOL connected = ConnectNamedPipe(pipe, &ov);
            if (!connected) {
                const DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    HANDLE waits[2] = {_stopEvent, ioEvent};
                    const DWORD wait = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
                    if (wait != WAIT_OBJECT_0 + 1) {
                        CancelIoEx(pipe, &ov);
                        CloseHandle(pipe);
                        break;
                    }
                    DWORD ignored = 0;
                    connected = GetOverlappedResult(pipe, &ov, &ignored, FALSE);
                }
                else if (err == ERROR_PIPE_CONNECTED) {
                    connected = TRUE;
                }
            }

            if (connected) {
                HandleClient(pipe, ioEvent);
                FlushFileBuffers(pipe);
                DisconnectNamedPipe(pipe);
            }
            CloseHandle(pipe);
        }

        CloseHandle(ioEvent);
    }

    void HandleClient(HANDLE pipe, HANDLE ioEvent)
    {
        std::array<char, kMaxMessageBytes> buffer{};
        ResetEvent(ioEvent);
        OVERLAPPED ov{};
        ov.hEvent = ioEvent;

        DWORD bytesRead = 0;
        BOOL ok = ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            HANDLE waits[2] = {_stopEvent, ioEvent};
            if (WaitForMultipleObjects(2, waits, FALSE, 1500) == WAIT_OBJECT_0 + 1) {
                ok = GetOverlappedResult(pipe, &ov, &bytesRead, FALSE);
            }
            else {
                CancelIoEx(pipe, &ov);
                return;
            }
        }
        if (!ok || bytesRead == 0) {
            return;
        }

        const QByteArray request{buffer.data(), static_cast<int>(bytesRead)};
        const QByteArray response =
            _handler ? _handler(request) : QByteArrayLiteral("{\"ok\":false}");

        ResetEvent(ioEvent);
        OVERLAPPED writeOv{};
        writeOv.hEvent = ioEvent;
        DWORD written = 0;
        const DWORD toWrite =
            static_cast<DWORD>(std::min<int>(response.size(), static_cast<int>(kMaxMessageBytes)));
        if (!WriteFile(pipe, response.constData(), toWrite, &written, &writeOv) &&
            GetLastError() == ERROR_IO_PENDING)
        {
            HANDLE waits[2] = {_stopEvent, ioEvent};
            if (WaitForMultipleObjects(2, waits, FALSE, 1500) == WAIT_OBJECT_0 + 1) {
                GetOverlappedResult(pipe, &writeOv, &written, FALSE);
            }
            else {
                CancelIoEx(pipe, &writeOv);
            }
        }
    }

    Handler _handler;
    std::wstring _pipeName;
    std::atomic<bool> _running{false};
    HANDLE _stopEvent{nullptr};
    HANDLE _readyEvent{nullptr};
    std::thread _thread;
};

Server::Server(Handler handler) : _impl{std::make_unique<Impl>(std::move(handler))} {}
Server::~Server() = default;

bool Server::Start()
{
    return _impl->Start();
}

void Server::Stop()
{
    _impl->Stop();
}

std::optional<QByteArray>
SendCommand(const QByteArray &requestJson, std::chrono::milliseconds timeout)
{
    const std::wstring pipeName = PipeNameForCurrentUser();
    const DWORD timeoutMs = static_cast<DWORD>(std::max<int64_t>(100, timeout.count()));

    if (WaitNamedPipeW(pipeName.c_str(), timeoutMs) == FALSE) {
        return std::nullopt;
    }

    HANDLE pipe = CreateFileW(
        pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) {
        return std::nullopt;
    }

    DWORD mode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(pipe, &mode, nullptr, nullptr);

    DWORD written = 0;
    const DWORD toWrite =
        static_cast<DWORD>(std::min<int>(requestJson.size(), static_cast<int>(kMaxMessageBytes)));
    const BOOL writeOk = WriteFile(pipe, requestJson.constData(), toWrite, &written, nullptr);
    if (!writeOk || written != toWrite) {
        CloseHandle(pipe);
        return std::nullopt;
    }

    std::array<char, kMaxMessageBytes> buffer{};
    DWORD bytesRead = 0;
    const BOOL readOk =
        ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
    CloseHandle(pipe);

    if (!readOk && GetLastError() != ERROR_MORE_DATA) {
        return std::nullopt;
    }
    return QByteArray{buffer.data(), static_cast<int>(bytesRead)};
}

} // namespace Core::Ipc
