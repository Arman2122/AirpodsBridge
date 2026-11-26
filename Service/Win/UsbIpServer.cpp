// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/UsbIpServer.h"
#include "Service/Win/RadioDriverManager.h"
#include "Service/Win/ServiceLog.h"

#include <cstring>
#include <regex>
#include <vector>

namespace Service::Win {

namespace {

std::once_flag gWsaInitOnce;

void EnsureWinsock()
{
    std::call_once(gWsaInitOnce, [] {
        WSADATA wsa{};
        WSAStartup(MAKEWORD(2, 2), &wsa);
    });
}

} // namespace

UsbIpServer::UsbIpServer()
{
    EnsureWinsock();
}

UsbIpServer::~UsbIpServer()
{
    Stop();
}

bool UsbIpServer::Start(ImportCallback onImport)
{
    Stop();
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _onImport = std::move(onImport);
    }

    UniqueSocket sock{socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)};
    if (!sock.Valid()) {
        ServiceLog::Write(LogLevel::Error, "usbip", "Failed to create listen socket.");
        return false;
    }

    BOOL nodelay = TRUE;
    setsockopt(
        sock.Get(), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay),
        sizeof(nodelay));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // Strictly 127.0.0.1 only
    addr.sin_port = 0;                             // Ephemeral local port

    if (bind(sock.Get(), reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) == SOCKET_ERROR ||
        listen(sock.Get(), 8) == SOCKET_ERROR)
    {
        ServiceLog::Write(
            LogLevel::Error, "usbip",
            "bind/listen on 127.0.0.1 failed: WSA " + std::to_string(WSAGetLastError()));
        return false;
    }

    int addrLen = sizeof(addr);
    if (getsockname(sock.Get(), reinterpret_cast<sockaddr *>(&addr), &addrLen) == SOCKET_ERROR) {
        return false;
    }

    _port = ntohs(addr.sin_port);
    _listenSock = std::move(sock);
    _running.store(true);
    _acceptThread = std::thread{&UsbIpServer::AcceptLoop, this};

    ServiceLog::Write(
        LogLevel::Info, "usbip", "USB/IP server listening on 127.0.0.1:" + std::to_string(_port));
    return true;
}

void UsbIpServer::Stop()
{
    _running.store(false);
    if (_listenSock.Valid()) {
        shutdown(_listenSock.Get(), SD_BOTH);
        _listenSock.Reset();
    }
    if (_acceptThread.joinable()) {
        _acceptThread.join();
    }
}

void UsbIpServer::ExportDevice(const Protocol::UsbIp::ExportedDevice &device)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _devices[device.busId] = device;
}

void UsbIpServer::UnexportDevice(const std::string &busId)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _devices.erase(busId);
}

bool UsbIpServer::RecvExact(SOCKET s, uint8_t *dst, size_t len)
{
    size_t got = 0;
    while (got < len) {
        const int n = recv(
            s, reinterpret_cast<char *>(dst + got),
            static_cast<int>(std::min<size_t>(len - got, 1024 * 1024)), 0);
        if (n <= 0) {
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

bool UsbIpServer::SendAll(SOCKET s, const uint8_t *src, size_t len)
{
    size_t sent = 0;
    while (sent < len) {
        const int n = send(
            s, reinterpret_cast<const char *>(src + sent),
            static_cast<int>(std::min<size_t>(len - sent, 1024 * 1024)), 0);
        if (n <= 0) {
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

void UsbIpServer::AcceptLoop()
{
    while (_running.load()) {
        sockaddr_in peer{};
        int peerLen = sizeof(peer);
        SOCKET raw = accept(_listenSock.Get(), reinterpret_cast<sockaddr *>(&peer), &peerLen);
        if (raw == INVALID_SOCKET) {
            if (!_running.load()) {
                break;
            }
            continue;
        }

        // Belt-and-suspenders loopback verification.
        if (ntohl(peer.sin_addr.s_addr) != INADDR_LOOPBACK) {
            closesocket(raw);
            continue;
        }

        BOOL nodelay = TRUE;
        setsockopt(
            raw, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char *>(&nodelay),
            sizeof(nodelay));

        UniqueSocket client{raw};
        HandleHandshake(std::move(client));
    }
}

void UsbIpServer::HandleHandshake(UniqueSocket client)
{
    uint8_t header[8]{};
    if (!RecvExact(client.Get(), header, sizeof(header))) {
        return;
    }

    const uint16_t op = Protocol::UsbIp::ReadBe16(header, 2);
    if (op == Protocol::UsbIp::kOpReqDevList) {
        std::vector<Protocol::UsbIp::ExportedDevice> list;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            for (const auto &[busId, dev] : _devices) {
                list.push_back(dev);
            }
        }
        const auto reply = Protocol::UsbIp::EncodeOpRepDevList(list);
        SendAll(client.Get(), reply.data(), reply.size());
        return;
    }

    if (op == Protocol::UsbIp::kOpReqImport) {
        char busIdBuf[33]{};
        if (!RecvExact(client.Get(), reinterpret_cast<uint8_t *>(busIdBuf), 32)) {
            return;
        }
        const std::string busId{busIdBuf};

        std::optional<Protocol::UsbIp::ExportedDevice> found;
        ImportCallback cb;
        {
            std::lock_guard<std::mutex> lock{_mutex};
            if (const auto it = _devices.find(busId); it != _devices.end()) {
                found = it->second;
            }
            cb = _onImport;
        }

        if (!found.has_value()) {
            const auto errReply = Protocol::UsbIp::EncodeOpRepImport(nullptr, 1);
            SendAll(client.Get(), errReply.data(), errReply.size());
            ServiceLog::Write(
                LogLevel::Warn, "usbip", "Import requested for unknown busId: " + busId);
            return;
        }

        const auto okReply = Protocol::UsbIp::EncodeOpRepImport(&found.value(), 0);
        if (!SendAll(client.Get(), okReply.data(), okReply.size())) {
            return;
        }

        ServiceLog::Write(LogLevel::Info, "usbip", "Imported virtual device " + busId);
        if (cb) {
            cb(busId, std::move(client));
        }
    }
}

bool UsbIpServer::RunUsbipCommand(const std::wstring &args, std::string &outputOut, DWORD timeoutMs)
{
    outputOut.clear();
    const auto exe = RadioDriverManager::FindUsbipExecutable();
    if (!exe.has_value()) {
        ServiceLog::Write(LogLevel::Error, "usbip", "usbip.exe not found.");
        return false;
    }

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE rawRead = nullptr;
    HANDLE rawWrite = nullptr;
    if (!CreatePipe(&rawRead, &rawWrite, &sa, 0)) {
        return false;
    }
    UniqueHandle readPipe{rawRead};
    UniqueHandle writePipe{rawWrite};
    SetHandleInformation(readPipe.Get(), HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe.Get();
    si.hStdError = writePipe.Get();

    PROCESS_INFORMATION pi{};
    std::wstring cmdLine = L"\"" + *exe + L"\" " + args;
    if (!CreateProcessW(
            nullptr, cmdLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr,
            &si, &pi))
    {
        ServiceLog::Write(
            LogLevel::Error, "usbip",
            "CreateProcessW(usbip.exe) failed: " + FormatWin32Error(GetLastError()));
        return false;
    }
    UniqueHandle proc{pi.hProcess};
    UniqueHandle thread{pi.hThread};
    writePipe.Reset();

    const DWORD wait = WaitForSingleObject(proc.Get(), timeoutMs);
    if (wait != WAIT_OBJECT_0) {
        TerminateProcess(proc.Get(), 1);
        ServiceLog::Write(LogLevel::Error, "usbip", "usbip.exe command timed out.");
        return false;
    }

    char chunk[512]{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe.Get(), chunk, sizeof(chunk), &bytesRead, nullptr) && bytesRead > 0) {
        outputOut.append(chunk, bytesRead);
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(proc.Get(), &exitCode);
    return exitCode == 0;
}

std::optional<int> UsbIpServer::AttachLocalDevice(const std::string &busId)
{
    const std::wstring args = L"--tcp-port " + std::to_wstring(_port) +
                              L" attach -r 127.0.0.1 -b " + Utf8ToWide(busId) +
                              L" --once --receive-mode low-latency";
    std::string out;
    if (!RunUsbipCommand(args, out, 15000)) {
        ServiceLog::Write(
            LogLevel::Error, "usbip", "usbip attach failed for " + busId + ": " + out);
        return std::nullopt;
    }

    static const std::regex kPortRe{R"(port\s+(\d+))", std::regex_constants::icase};
    std::smatch m;
    if (std::regex_search(out, m, kPortRe)) {
        return std::stoi(m[1].str());
    }
    return 1;
}

bool UsbIpServer::DetachLocalPort(int usbipPort)
{
    if (usbipPort <= 0) {
        return true;
    }
    std::string out;
    const bool ok = RunUsbipCommand(L"detach -p " + std::to_wstring(usbipPort), out, 10000);
    if (!ok) {
        ServiceLog::Write(
            LogLevel::Warn, "usbip",
            "usbip detach -p " + std::to_string(usbipPort) + " returned error: " + out);
    }
    return ok;
}

} // namespace Service::Win
