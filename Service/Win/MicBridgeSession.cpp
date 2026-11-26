// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Win/MicBridgeSession.h"
#include "Service/Win/ServiceLog.h"
#include "Service/Win/UsbIpServer.h"

#include <avrt.h>

namespace Service::Win {

namespace {

int64_t QpcNow()
{
    LARGE_INTEGER li{};
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

int64_t QpcFrequency()
{
    LARGE_INTEGER li{};
    QueryPerformanceFrequency(&li);
    return li.QuadPart > 0 ? li.QuadPart : 10000000ll;
}

void WaitUntilQpc(int64_t targetQpc, HANDLE highResTimer)
{
    const int64_t freq = QpcFrequency();
    for (;;) {
        const int64_t now = QpcNow();
        const int64_t remain = targetQpc - now;
        if (remain <= 0) {
            return;
        }
        const int64_t remainUs = (remain * 1000000ll) / freq;
        if (remainUs > 300 && highResTimer != nullptr) {
            LARGE_INTEGER due{};
            // Negative relative time in 100 ns units; wake slightly early for spin finish.
            due.QuadPart = -((remainUs - 150) * 10ll);
            if (SetWaitableTimerEx(highResTimer, &due, 0, nullptr, nullptr, nullptr, 0)) {
                WaitForSingleObject(highResTimer, 20);
                continue;
            }
        }
        YieldProcessor();
    }
}

} // namespace

MicBridgeSession::MicBridgeSession(UniqueSocket clientSocket, Protocol::Uac1MicEmulator &emulator)
    : _socket{std::move(clientSocket)}, _emulator{emulator}
{
}

MicBridgeSession::~MicBridgeSession()
{
    Stop();
}

void MicBridgeSession::Stop()
{
    if (_stopped.exchange(true)) {
        return;
    }
    if (_socket.Valid()) {
        shutdown(_socket.Get(), SD_BOTH);
    }
    _jobsCv.notify_all();
    if (_pacerThread.joinable() && _pacerThread.get_id() != std::this_thread::get_id()) {
        _pacerThread.join();
    }
    _socket.Reset();
}

void MicBridgeSession::Run()
{
    _pacerThread = std::thread{&MicBridgeSession::PacerLoop, this};

    const int64_t freq = QpcFrequency();
    int64_t nextDueQpc = QpcNow();
    uint32_t frameCounter = 0;

    while (!_stopped.load()) {
        uint8_t hdrBytes[Protocol::UsbIp::kUrbHeaderBytes]{};
        if (!UsbIpServer::RecvExact(_socket.Get(), hdrBytes, sizeof(hdrBytes))) {
            break;
        }

        const auto hdrOpt = Protocol::UsbIp::DecodeCommandHeader(hdrBytes);
        if (!hdrOpt.has_value()) {
            break;
        }
        const auto &hdr = *hdrOpt;

        if (hdr.command == Protocol::UsbIp::kCmdUnlink) {
            {
                std::lock_guard<std::mutex> lock{_jobsMutex};
                _unlinkedSeqs.insert(hdr.unlinkSeqnum);
            }
            if (!SendReply(Protocol::UsbIp::EncodeRetUnlink(hdr.seqnum, 0))) {
                break;
            }
            continue;
        }

        std::vector<uint8_t> outPayload;
        if (hdr.direction == Protocol::UsbIp::kDirOut && hdr.transferBufferLength > 0) {
            if (hdr.transferBufferLength > 64u * 1024u) {
                break;
            }
            outPayload.resize(hdr.transferBufferLength);
            if (!UsbIpServer::RecvExact(_socket.Get(), outPayload.data(), outPayload.size())) {
                break;
            }
        }

        std::vector<uint8_t> isoDescs;
        if (hdr.IsIsochronous()) {
            if (hdr.numberOfPackets > 256) {
                break;
            }
            isoDescs.resize(
                static_cast<size_t>(hdr.numberOfPackets) * Protocol::UsbIp::kIsoDescriptorBytes);
            if (!UsbIpServer::RecvExact(_socket.Get(), isoDescs.data(), isoDescs.size())) {
                break;
            }
        }

        if (hdr.endpoint == 0) {
            const auto ctrl = _emulator.HandleControlTransfer(hdr.setup, outPayload);
            const auto reply = Protocol::UsbIp::EncodeRetSubmit(
                hdr.seqnum, ctrl.status, static_cast<uint32_t>(ctrl.data.size()),
                hdr.direction == Protocol::UsbIp::kDirIn ? std::span<const uint8_t>{ctrl.data}
                                                         : std::span<const uint8_t>{});
            if (!SendReply(reply)) {
                break;
            }
            continue;
        }

        if (hdr.IsIsochronous() && hdr.direction == Protocol::UsbIp::kDirIn) {
            const int64_t now = QpcNow();
            if (nextDueQpc < now) {
                nextDueQpc = now;
            }
            nextDueQpc += (static_cast<int64_t>(hdr.numberOfPackets) * freq) / 1000ll;

            IsoJob job{};
            job.seqnum = hdr.seqnum;
            job.packetCount = hdr.numberOfPackets;
            job.startFrame = frameCounter;
            frameCounter = (frameCounter + hdr.numberOfPackets) & 0x7FFu;
            job.cmdDescriptors = std::move(isoDescs);
            job.dueQpc = nextDueQpc;

            {
                std::lock_guard<std::mutex> lock{_jobsMutex};
                _jobs.push_back(std::move(job));
            }
            _jobsCv.notify_one();
            continue;
        }

        if (!SendReply(
                Protocol::UsbIp::EncodeRetSubmit(hdr.seqnum, Protocol::UsbIp::kErrPipe, 0, {})))
        {
            break;
        }
    }

    Stop();
}

void MicBridgeSession::PacerLoop()
{
    DWORD taskIdx = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIdx);
    UniqueHandle timer{CreateWaitableTimerExW(
        nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS)};

    while (!_stopped.load()) {
        IsoJob job{};
        {
            std::unique_lock<std::mutex> lock{_jobsMutex};
            _jobsCv.wait(lock, [&] { return _stopped.load() || !_jobs.empty(); });
            if (_stopped.load()) {
                break;
            }
            job = std::move(_jobs.front());
            _jobs.pop_front();
        }

        WaitUntilQpc(job.dueQpc, timer.Get());
        if (_stopped.load()) {
            break;
        }

        bool unlinked = false;
        {
            std::lock_guard<std::mutex> lock{_jobsMutex};
            unlinked = _unlinkedSeqs.erase(job.seqnum) > 0;
        }
        if (unlinked) {
            SendReply(Protocol::UsbIp::EncodeRetSubmit(
                job.seqnum, Protocol::UsbIp::kErrConnReset, 0, {}));
            continue;
        }

        const auto iso = _emulator.FillIsochronousIn(job.packetCount);
        const auto frame = Protocol::UsbIp::EncodeRetIso(
            job.seqnum, Protocol::UsbIp::kErrOk, job.startFrame, job.cmdDescriptors,
            job.packetCount, true, &iso);
        if (!SendReply(frame)) {
            break;
        }
    }

    if (mmcss != nullptr) {
        AvRevertMmThreadCharacteristics(mmcss);
    }
}

bool MicBridgeSession::SendReply(std::span<const uint8_t> frame)
{
    std::lock_guard<std::mutex> lock{_writeMutex};
    if (!_socket.Valid()) {
        return false;
    }
    return UsbIpServer::SendAll(_socket.Get(), frame.data(), frame.size());
}

} // namespace Service::Win
