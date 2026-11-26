// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Service/Protocol/HciProxyEngine.h"
#include "Service/Protocol/UsbIpProtocol.h"
#include "Service/Win/WinHandles.h"
#include "Service/Win/WinUsbRadio.h"

namespace Service::Win {

//
// Bridges one imported USB/IP connection from `usbip-win2` to the WinUSB-bound Bluetooth
// radio, passing HCI commands, events, and ACL data through `HciProxyEngine`.
//
// Each USB pipe runs its own dedicated worker thread so completions stay strictly in-order
// (matching a real USB host controller) and slow pipes never block HCI events or control EP0.
//
class RadioBridgeSession
{
public:
    RadioBridgeSession(
        UniqueSocket clientSocket, WinUsbRadio &radio, Protocol::HciProxyEngine &hci);
    ~RadioBridgeSession();

    RadioBridgeSession(const RadioBridgeSession &) = delete;
    RadioBridgeSession &operator=(const RadioBridgeSession &) = delete;

    // Runs synchronously on the caller's thread until the socket closes, device is lost, or
    // Stop() is called.
    void Run();
    void Stop();

    // Enqueues an HCI ACL packet synthesized by `HciProxyEngine` onto the Bulk OUT worker.
    void EnqueueInjectedAcl(std::vector<uint8_t> aclPacket);

    // Wakes the Bulk OUT worker so it pulls ready injected ACL packets strictly at L2CAP PDU
    // boundaries right before writing to WinUSB (pull-mode zero-interleaving injection).
    void NotifyInjectedAclAvailable();

    // Enqueues an HCI command synthesized by `HciProxyEngine` onto the Control EP0 worker.
    void EnqueueInjectedCommand(std::vector<uint8_t> hciCommand);

    bool DeviceLost() const noexcept
    {
        return _deviceLost.load();
    }

private:
    struct PendingUrb {
        Protocol::UsbIp::CommandHeader header;
        std::vector<uint8_t> outPayload;
        std::vector<uint8_t> isoDescriptors;
        bool internal{false};
        bool internalCommand{false};
        UniqueHandle cancelEvent;
        std::atomic<bool> unlinked{false};
    };

    struct PipeQueue {
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<std::shared_ptr<PendingUrb>> queue;
        bool injectedPending{false};
        bool stopped{false};
        std::thread worker;
    };

    std::shared_ptr<PipeQueue> GetOrCreatePipeQueue(uint8_t pipeAddr);
    void PipeWorkerLoop(uint8_t pipeAddr, std::shared_ptr<PipeQueue> pq);
    void DrainInjectedAcl(uint8_t pipeAddr);
    void ExecuteUrb(uint8_t pipeAddr, const std::shared_ptr<PendingUrb> &urb);
    void HandleWritePipeFailure(uint8_t pipeAddr, size_t len, uint32_t err, const char *origin);
    bool SendReply(std::span<const uint8_t> frame);
    void MarkDeviceLost(uint32_t win32Err);

    UniqueSocket _socket;
    WinUsbRadio &_radio;
    Protocol::HciProxyEngine &_hci;
    Protocol::UsbIp::BluetoothHciPipes _hciPipes;
    UniqueHandle _stopEvent;

    std::atomic<bool> _stopped{false};
    std::atomic<bool> _deviceLost{false};
    std::atomic<uint32_t> _consecutiveAclOutTimeouts{0};
    std::mutex _writeMutex;

    std::mutex _pipesMutex;
    std::unordered_map<uint8_t, std::shared_ptr<PipeQueue>> _pipes;

    std::mutex _liveMutex;
    std::unordered_map<uint32_t, std::shared_ptr<PendingUrb>> _liveUrbs;
};

} // namespace Service::Win
