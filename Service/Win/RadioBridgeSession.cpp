// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/RadioBridgeSession.h"
#include "Service/Win/ServiceLog.h"
#include "Service/Win/UsbIpServer.h"

#include <avrt.h>

#include <algorithm>
#include <cstdio>

namespace Service::Win {

namespace {

constexpr uint32_t kMaxConsecutiveAclOutTimeouts = 4;

bool IsHardwareStallError(uint32_t win32Error) noexcept
{
    return win32Error == ERROR_GEN_FAILURE || win32Error == ERROR_IO_DEVICE ||
           win32Error == ERROR_CRC;
}

bool IsHciCommandSetup(const std::array<uint8_t, 8> &setup)
{
    // Bluetooth USB Class Request on EP0: bmRequestType = 0x20 (Class, Interface, OUT), bRequest =
    // 0x00.
    return setup[0] == 0x20 && setup[1] == 0x00;
}

bool IsSetInterfaceSetup(const std::array<uint8_t, 8> &setup)
{
    return setup[0] == 0x01 && setup[1] == 0x0B;
}

bool IsClearFeatureHalt(const std::array<uint8_t, 8> &setup)
{
    return setup[0] == 0x02 && setup[1] == 0x01 && setup[2] == 0x00 && setup[3] == 0x00;
}

} // namespace

RadioBridgeSession::RadioBridgeSession(
    UniqueSocket clientSocket, WinUsbRadio &radio, Protocol::HciProxyEngine &hci)
    : _socket{std::move(clientSocket)}, _radio{radio}, _hci{hci}, _hciPipes{radio.HciPipes()},
      _stopEvent{CreateEventW(nullptr, TRUE, FALSE, nullptr)}
{
}

RadioBridgeSession::~RadioBridgeSession()
{
    Stop();
}

void RadioBridgeSession::Stop()
{
    if (_stopped.exchange(true)) {
        return;
    }

    if (_stopEvent.Valid()) {
        SetEvent(_stopEvent.Get());
    }

    if (_socket.Valid()) {
        shutdown(_socket.Get(), SD_BOTH);
    }

    {
        std::lock_guard<std::mutex> lock{_liveMutex};
        for (auto &[seq, urb] : _liveUrbs) {
            urb->unlinked.store(true);
            if (urb->cancelEvent.Valid()) {
                SetEvent(urb->cancelEvent.Get());
            }
        }
    }

    _radio.AbortAllPipes();

    std::vector<std::shared_ptr<PipeQueue>> queues;
    {
        std::lock_guard<std::mutex> lock{_pipesMutex};
        for (auto &[pipe, pq] : _pipes) {
            {
                std::lock_guard<std::mutex> qlock{pq->mutex};
                pq->stopped = true;
            }
            pq->cv.notify_all();
            queues.push_back(pq);
        }
    }

    for (auto &pq : queues) {
        if (pq->worker.joinable() && pq->worker.get_id() != std::this_thread::get_id()) {
            pq->worker.join();
        }
    }
    _socket.Reset();
}

void RadioBridgeSession::Run()
{
    while (!_stopped.load()) {
        uint8_t hdrBytes[Protocol::UsbIp::kUrbHeaderBytes]{};
        if (!UsbIpServer::RecvExact(_socket.Get(), hdrBytes, sizeof(hdrBytes))) {
            break;
        }

        const auto hdrOpt = Protocol::UsbIp::DecodeCommandHeader(hdrBytes);
        if (!hdrOpt.has_value()) {
            ServiceLog::Write(LogLevel::Warn, "radio", "Received invalid USB/IP command header.");
            break;
        }

        const auto &hdr = *hdrOpt;
        if (hdr.command == Protocol::UsbIp::kCmdUnlink) {
            std::shared_ptr<PendingUrb> target;
            {
                std::lock_guard<std::mutex> lock{_liveMutex};
                if (const auto it = _liveUrbs.find(hdr.unlinkSeqnum); it != _liveUrbs.end()) {
                    target = it->second;
                }
            }
            if (target) {
                target->unlinked.store(true);
                if (target->cancelEvent.Valid()) {
                    SetEvent(target->cancelEvent.Get());
                }
            }
            const auto reply = Protocol::UsbIp::EncodeRetUnlink(
                hdr.seqnum, target ? Protocol::UsbIp::kErrConnReset : 0);
            if (!SendReply(reply)) {
                break;
            }
            continue;
        }

        auto urb = std::make_shared<PendingUrb>();
        urb->header = hdr;
        urb->cancelEvent = UniqueHandle{CreateEventW(nullptr, TRUE, FALSE, nullptr)};

        if (hdr.direction == Protocol::UsbIp::kDirOut && hdr.transferBufferLength > 0) {
            if (hdr.transferBufferLength > 2u * 1024u * 1024u) {
                break;
            }
            urb->outPayload.resize(hdr.transferBufferLength);
            if (!UsbIpServer::RecvExact(
                    _socket.Get(), urb->outPayload.data(), urb->outPayload.size()))
            {
                break;
            }
        }

        if (hdr.IsIsochronous()) {
            if (hdr.numberOfPackets > 1024) {
                break;
            }
            urb->isoDescriptors.resize(
                static_cast<size_t>(hdr.numberOfPackets) * Protocol::UsbIp::kIsoDescriptorBytes);
            if (!UsbIpServer::RecvExact(
                    _socket.Get(), urb->isoDescriptors.data(), urb->isoDescriptors.size()))
            {
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock{_liveMutex};
            _liveUrbs[hdr.seqnum] = urb;
        }

        const uint8_t pipeAddr = hdr.endpoint == 0 ? 0u : hdr.PipeAddress();
        auto pq = GetOrCreatePipeQueue(pipeAddr);
        {
            std::lock_guard<std::mutex> qlock{pq->mutex};
            pq->queue.push_back(std::move(urb));
        }
        pq->cv.notify_one();
    }

    Stop();
}

void RadioBridgeSession::EnqueueInjectedAcl(std::vector<uint8_t> aclPacket)
{
    if (_stopped.load() || _hciPipes.aclOutPipe == 0 || aclPacket.empty()) {
        return;
    }

    auto urb = std::make_shared<PendingUrb>();
    urb->internal = true;
    urb->outPayload = std::move(aclPacket);

    auto pq = GetOrCreatePipeQueue(_hciPipes.aclOutPipe);
    {
        std::lock_guard<std::mutex> qlock{pq->mutex};
        if (pq->stopped) {
            return;
        }
        pq->queue.push_back(std::move(urb));
    }
    pq->cv.notify_one();
}

void RadioBridgeSession::NotifyInjectedAclAvailable()
{
    if (_stopped.load() || _hciPipes.aclOutPipe == 0) {
        return;
    }

    auto pq = GetOrCreatePipeQueue(_hciPipes.aclOutPipe);
    {
        std::lock_guard<std::mutex> qlock{pq->mutex};
        if (pq->stopped) {
            return;
        }
        pq->injectedPending = true;
    }
    pq->cv.notify_one();
}

void RadioBridgeSession::EnqueueInjectedCommand(std::vector<uint8_t> hciCommand)
{
    if (_stopped.load() || hciCommand.empty()) {
        return;
    }

    auto urb = std::make_shared<PendingUrb>();
    urb->internal = true;
    urb->internalCommand = true;
    urb->outPayload = std::move(hciCommand);

    auto pq = GetOrCreatePipeQueue(0);
    {
        std::lock_guard<std::mutex> qlock{pq->mutex};
        if (pq->stopped) {
            return;
        }
        pq->queue.push_back(std::move(urb));
    }
    pq->cv.notify_one();
}

std::shared_ptr<RadioBridgeSession::PipeQueue>
RadioBridgeSession::GetOrCreatePipeQueue(uint8_t pipeAddr)
{
    std::lock_guard<std::mutex> lock{_pipesMutex};
    if (const auto it = _pipes.find(pipeAddr); it != _pipes.end()) {
        return it->second;
    }
    auto pq = std::make_shared<PipeQueue>();
    pq->worker = std::thread{&RadioBridgeSession::PipeWorkerLoop, this, pipeAddr, pq};
    _pipes.emplace(pipeAddr, pq);
    return pq;
}

void RadioBridgeSession::PipeWorkerLoop(uint8_t pipeAddr, std::shared_ptr<PipeQueue> pq)
{
    DWORD taskIdx = 0;
    HANDLE mmcss = nullptr;
    if (pipeAddr == _hciPipes.eventPipe || pipeAddr != 0) {
        mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIdx);
    }

    for (;;) {
        std::shared_ptr<PendingUrb> urb;
        bool drainInjected = false;
        {
            std::unique_lock<std::mutex> qlock{pq->mutex};
            pq->cv.wait(
                qlock, [&] { return pq->stopped || !pq->queue.empty() || pq->injectedPending; });
            if (pq->stopped) {
                break;
            }
            if (!pq->queue.empty()) {
                urb = std::move(pq->queue.front());
                pq->queue.pop_front();
            }
            else if (pq->injectedPending) {
                pq->injectedPending = false;
                drainInjected = true;
            }
        }

        if (urb) {
            ExecuteUrb(pipeAddr, urb);

            if (!urb->internal) {
                std::lock_guard<std::mutex> lock{_liveMutex};
                _liveUrbs.erase(urb->header.seqnum);
            }
            if (pipeAddr == _hciPipes.aclOutPipe) {
                DrainInjectedAcl(pipeAddr);
            }
        }
        else if (drainInjected && pipeAddr == _hciPipes.aclOutPipe) {
            DrainInjectedAcl(pipeAddr);
        }
    }

    if (mmcss != nullptr) {
        AvRevertMmThreadCharacteristics(mmcss);
    }
}

void RadioBridgeSession::DrainInjectedAcl(uint8_t pipeAddr)
{
    std::vector<uint8_t> pkt;
    uint16_t handle = 0;
    while (!_stopped.load() && _hci.PopNextInjectedAcl(pkt, handle)) {
        uint32_t written = 0;
        const uint32_t err = _radio.WritePipe(pipeAddr, pkt, written, _stopEvent.Get());
        if (err != ERROR_SUCCESS) {
            _hci.RevertInjectedAcl(handle);
            HandleWritePipeFailure(pipeAddr, pkt.size(), err, "injected HCI ACL");
            break;
        }
        if (pipeAddr == _hciPipes.aclOutPipe) {
            _consecutiveAclOutTimeouts.store(0);
        }
    }
}

void RadioBridgeSession::HandleWritePipeFailure(
    uint8_t pipeAddr, size_t len, uint32_t err, const char *origin)
{
    if (err == ERROR_OPERATION_ABORTED || _stopped.load()) {
        return;
    }

    char prefix[160]{};
    std::snprintf(
        prefix, sizeof(prefix), "Bulk OUT write failed (%s, pipe=0x%02X, len=%zu): ", origin,
        pipeAddr, len);
    ServiceLog::Write(LogLevel::Warn, "radio", std::string{prefix} + FormatWin32Error(err));

    if (IsHardwareStallError(err)) {
        // Only call WinUsb_ResetPipe on genuine USB STALL / I/O errors — NEVER on a simple
        // ERROR_SEM_TIMEOUT (flow-control NAK), because ResetPipe resets the host USB Data Toggle
        // while a non-halted controller stays on its previous toggle and drops every later packet.
        const uint32_t resetErr = _radio.ResetPipe(pipeAddr);
        char resetBuf[160]{};
        std::snprintf(resetBuf, sizeof(resetBuf), "ResetPipe(0x%02X) after OUT stall: ", pipeAddr);
        ServiceLog::Write(
            resetErr == ERROR_SUCCESS ? LogLevel::Info : LogLevel::Warn, "radio",
            std::string{resetBuf} +
                (resetErr == ERROR_SUCCESS ? "ok" : FormatWin32Error(resetErr)));
    }
    else if (pipeAddr == _hciPipes.aclOutPipe && err == ERROR_SEM_TIMEOUT) {
        const uint32_t streak = _consecutiveAclOutTimeouts.fetch_add(1) + 1;
        if (streak >= kMaxConsecutiveAclOutTimeouts) {
            ServiceLog::Write(
                LogLevel::Error, "radio",
                "HCI Bulk OUT pipe timed out " + std::to_string(streak) +
                    " times consecutively; recycling radio session.");
            MarkDeviceLost(err);
            return;
        }
    }

    if (WinUsbRadio::IsFatalDeviceLossError(err)) {
        MarkDeviceLost(err);
    }
}

void RadioBridgeSession::ExecuteUrb(uint8_t pipeAddr, const std::shared_ptr<PendingUrb> &urb)
{
    if (urb->unlinked.load()) {
        if (!urb->internal) {
            SendReply(Protocol::UsbIp::EncodeRetSubmit(
                urb->header.seqnum, Protocol::UsbIp::kErrConnReset, 0, {}));
        }
        return;
    }

    // Internal injected ACL packet (hidden from Windows).
    if (urb->internal && !urb->internalCommand) {
        uint32_t written = 0;
        const uint32_t err = _radio.WritePipe(pipeAddr, urb->outPayload, written, _stopEvent.Get());
        if (err == ERROR_SUCCESS) {
            if (pipeAddr == _hciPipes.aclOutPipe) {
                _consecutiveAclOutTimeouts.store(0);
            }
        }
        else {
            HandleWritePipeFailure(pipeAddr, urb->outPayload.size(), err, "queued internal ACL");
        }
        return;
    }

    // Internal injected HCI command on EP0 (hidden from Windows).
    if (urb->internal && urb->internalCommand) {
        std::array<uint8_t, 8> setup{
            0x20,
            0x00,
            0x00,
            0x00,
            _hciPipes.interfaceNumber,
            0x00,
            static_cast<uint8_t>(urb->outPayload.size() & 0xFFu),
            static_cast<uint8_t>((urb->outPayload.size() >> 8) & 0xFFu),
        };
        std::vector<uint8_t> ignored;
        uint32_t xfer = 0;
        const uint32_t err =
            _radio.ControlTransfer(setup, urb->outPayload, ignored, xfer, _stopEvent.Get());
        if (err != ERROR_SUCCESS && err != ERROR_OPERATION_ABORTED) {
            ServiceLog::Write(
                LogLevel::Warn, "radio",
                "Injected HCI command on EP0 failed: " + FormatWin32Error(err));
        }
        if (WinUsbRadio::IsFatalDeviceLossError(err)) {
            MarkDeviceLost(err);
        }
        return;
    }

    const auto &hdr = urb->header;

    // 1. Control EP0 transfers (standard USB + Bluetooth HCI commands).
    if (pipeAddr == 0) {
        if (IsSetInterfaceSetup(hdr.setup)) {
            const uint8_t alt = hdr.setup[2];
            const uint8_t iface = hdr.setup[4];
            const uint32_t err = _radio.SetInterfaceAltSetting(iface, alt);
            if (WinUsbRadio::IsFatalDeviceLossError(err)) {
                MarkDeviceLost(err);
            }
            SendReply(Protocol::UsbIp::EncodeRetSubmit(
                hdr.seqnum, Protocol::UsbIp::Win32ErrorToErrno(err), 0, {}));
            return;
        }

        if (IsClearFeatureHalt(hdr.setup)) {
            const uint8_t targetPipe = hdr.setup[4];
            const uint32_t err = _radio.ResetPipe(targetPipe);
            SendReply(Protocol::UsbIp::EncodeRetSubmit(
                hdr.seqnum, Protocol::UsbIp::Win32ErrorToErrno(err), 0, {}));
            return;
        }

        std::vector<uint8_t> outCopy = urb->outPayload;
        if (IsHciCommandSetup(hdr.setup) && !outCopy.empty()) {
            if (!_hci.ProcessWindowsCommand(outCopy)) {
                SendReply(Protocol::UsbIp::EncodeRetSubmit(
                    hdr.seqnum, Protocol::UsbIp::kErrOk, static_cast<uint32_t>(outCopy.size()),
                    {}));
                return;
            }
        }

        std::vector<uint8_t> inData;
        uint32_t transferred = 0;
        const uint32_t err =
            _radio.ControlTransfer(hdr.setup, outCopy, inData, transferred, urb->cancelEvent.Get());
        if (WinUsbRadio::IsFatalDeviceLossError(err)) {
            MarkDeviceLost(err);
        }
        SendReply(Protocol::UsbIp::EncodeRetSubmit(
            hdr.seqnum, Protocol::UsbIp::Win32ErrorToErrno(err), transferred,
            hdr.direction == Protocol::UsbIp::kDirIn ? std::span<const uint8_t>{inData}
                                                     : std::span<const uint8_t>{}));
        return;
    }

    // 2. Isochronous SCO/eSCO call audio pipes.
    if (hdr.IsIsochronous()) {
        const uint32_t pktCount = hdr.numberOfPackets;
        const uint32_t bytesPerPkt =
            pktCount > 0 ? std::max<uint32_t>(1, hdr.transferBufferLength / pktCount) : 1;
        Protocol::UsbIp::IsoTransferResult isoRes;
        const bool isIn = hdr.direction == Protocol::UsbIp::kDirIn;
        const uint32_t err = _radio.TransferIsoch(
            pipeAddr, isIn, urb->outPayload, pktCount, bytesPerPkt, isoRes, urb->cancelEvent.Get());
        if (WinUsbRadio::IsFatalDeviceLossError(err)) {
            MarkDeviceLost(err);
        }
        SendReply(Protocol::UsbIp::EncodeRetIso(
            hdr.seqnum, Protocol::UsbIp::Win32ErrorToErrno(err), hdr.startFrame,
            urb->isoDescriptors, pktCount, isIn, err == ERROR_SUCCESS ? &isoRes : nullptr));
        return;
    }

    // 3. Interrupt IN (HCI Events) & Bulk IN (HCI ACL Data).
    if (hdr.direction == Protocol::UsbIp::kDirIn) {
        std::vector<uint8_t> raw;
        std::vector<uint8_t> filtered;

        // Loop if an entire controller transfer was consumed by our hidden AAP engine so
        // Windows never receives spurious 0-byte completions on the HCI pipes.
        while (!_stopped.load() && !urb->unlinked.load()) {
            const uint32_t err =
                _radio.ReadPipe(pipeAddr, hdr.transferBufferLength, raw, urb->cancelEvent.Get());
            if (err != ERROR_SUCCESS) {
                if (WinUsbRadio::IsFatalDeviceLossError(err)) {
                    MarkDeviceLost(err);
                }
                SendReply(Protocol::UsbIp::EncodeRetSubmit(
                    hdr.seqnum, Protocol::UsbIp::Win32ErrorToErrno(err), 0, {}));
                return;
            }

            if (pipeAddr == _hciPipes.eventPipe) {
                _hci.ProcessControllerEvents(raw, filtered);
                if (filtered.empty() && !raw.empty()) {
                    continue; // Fully hidden event (e.g. our completed packets); read next event.
                }
                SendReply(Protocol::UsbIp::EncodeRetSubmit(
                    hdr.seqnum, Protocol::UsbIp::kErrOk, static_cast<uint32_t>(filtered.size()),
                    filtered));
                return;
            }

            if (pipeAddr == _hciPipes.aclInPipe) {
                _hci.ProcessControllerAclIn(raw, filtered);
                if (filtered.empty() && !raw.empty()) {
                    continue; // Fully hidden AAP packet; wait for next Windows ACL packet.
                }
                SendReply(Protocol::UsbIp::EncodeRetSubmit(
                    hdr.seqnum, Protocol::UsbIp::kErrOk, static_cast<uint32_t>(filtered.size()),
                    filtered));
                return;
            }

            SendReply(Protocol::UsbIp::EncodeRetSubmit(
                hdr.seqnum, Protocol::UsbIp::kErrOk, static_cast<uint32_t>(raw.size()), raw));
            return;
        }

        SendReply(
            Protocol::UsbIp::EncodeRetSubmit(hdr.seqnum, Protocol::UsbIp::kErrConnReset, 0, {}));
        return;
    }

    // 4. Bulk OUT (Windows HCI ACL Data).
    std::vector<uint8_t> toSend;
    if (pipeAddr == _hciPipes.aclOutPipe) {
        _hci.ProcessWindowsAclOut(urb->outPayload, toSend);
    }
    else {
        toSend = urb->outPayload;
    }

    if (toSend.empty()) {
        SendReply(Protocol::UsbIp::EncodeRetSubmit(
            hdr.seqnum, Protocol::UsbIp::kErrOk, static_cast<uint32_t>(urb->outPayload.size()),
            {}));
        return;
    }

    uint32_t written = 0;
    const uint32_t err = _radio.WritePipe(pipeAddr, toSend, written, urb->cancelEvent.Get());
    if (err == ERROR_SUCCESS) {
        if (pipeAddr == _hciPipes.aclOutPipe) {
            _consecutiveAclOutTimeouts.store(0);
        }
    }
    else {
        HandleWritePipeFailure(pipeAddr, toSend.size(), err, "Windows HCI ACL");
    }
    SendReply(Protocol::UsbIp::EncodeRetSubmit(
        hdr.seqnum, Protocol::UsbIp::Win32ErrorToErrno(err),
        err == ERROR_SUCCESS ? static_cast<uint32_t>(urb->outPayload.size()) : written, {}));
}

bool RadioBridgeSession::SendReply(std::span<const uint8_t> frame)
{
    std::lock_guard<std::mutex> lock{_writeMutex};
    if (!_socket.Valid()) {
        return false;
    }
    return UsbIpServer::SendAll(_socket.Get(), frame.data(), frame.size());
}

void RadioBridgeSession::MarkDeviceLost(uint32_t win32Err)
{
    if (!_deviceLost.exchange(true)) {
        ServiceLog::Write(
            LogLevel::Warn, "radio", "Physical WinUSB radio lost: " + FormatWin32Error(win32Err));
        std::lock_guard<std::mutex> lock{_writeMutex};
        if (_socket.Valid()) {
            shutdown(_socket.Get(), SD_BOTH);
        }
    }
}

} // namespace Service::Win
