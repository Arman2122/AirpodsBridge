// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "Service/Protocol/UsbIpProtocol.h"
#include "Service/Win/WinHandles.h"

namespace Service::Win {

//
// Localhost-only (127.0.0.1) USB/IP v1.1.1 TCP server paired with `usbip-win2`.
//
// Exports virtual devices by Bus ID (`1-1` = physical Bluetooth radio, `1-2` = virtual UAC1
// microphone) and transfers accepted import sockets directly to the owning bridge session.
//
class UsbIpServer
{
public:
    using ImportCallback = std::function<void(const std::string &busId, UniqueSocket clientSocket)>;

    UsbIpServer();
    ~UsbIpServer();

    UsbIpServer(const UsbIpServer &) = delete;
    UsbIpServer &operator=(const UsbIpServer &) = delete;

    bool Start(ImportCallback onImport);
    void Stop();
    uint16_t Port() const noexcept
    {
        return _port;
    }

    void ExportDevice(const Protocol::UsbIp::ExportedDevice &device);
    void UnexportDevice(const std::string &busId);

    // Invokes `usbip.exe` (installed by usbip-win2) to attach/detach an exported device on
    // 127.0.0.1:<port>. Returns the assigned usbip port number on attach, or std::nullopt.
    std::optional<int> AttachLocalDevice(const std::string &busId);
    static bool DetachLocalPort(int usbipPort);

    // Blocking exact socket I/O helpers shared by bridge sessions.
    static bool RecvExact(SOCKET s, uint8_t *dst, size_t len);
    static bool SendAll(SOCKET s, const uint8_t *src, size_t len);

private:
    void AcceptLoop();
    void HandleHandshake(UniqueSocket client);

    static bool RunUsbipCommand(const std::wstring &args, std::string &outputOut, DWORD timeoutMs);

    UniqueSocket _listenSock;
    uint16_t _port{0};
    std::atomic<bool> _running{false};
    std::thread _acceptThread;

    mutable std::mutex _mutex;
    ImportCallback _onImport;
    std::unordered_map<std::string, Protocol::UsbIp::ExportedDevice> _devices;
};

} // namespace Service::Win
