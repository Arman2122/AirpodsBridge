// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "Service/Protocol/Uac1MicEmulator.h"
#include "Service/Win/WinHandles.h"

namespace Service::Win {

//
// Serves a `Uac1MicEmulator` over a `usbip-win2` TCP session so Windows' inbox `usbaudio.sys`
// sees a real 48 kHz mono USB Audio Class 1.0 microphone with smooth 1 ms high-resolution
// isochronous pacing.
//
class MicBridgeSession
{
public:
    MicBridgeSession(UniqueSocket clientSocket, Protocol::Uac1MicEmulator &emulator);
    ~MicBridgeSession();

    MicBridgeSession(const MicBridgeSession &) = delete;
    MicBridgeSession &operator=(const MicBridgeSession &) = delete;

    void Run();
    void Stop();

private:
    struct IsoJob {
        uint32_t seqnum{0};
        uint32_t packetCount{0};
        uint32_t startFrame{0};
        std::vector<uint8_t> cmdDescriptors;
        int64_t dueQpc{0};
    };

    void PacerLoop();
    bool SendReply(std::span<const uint8_t> frame);

    UniqueSocket _socket;
    Protocol::Uac1MicEmulator &_emulator;

    std::atomic<bool> _stopped{false};
    std::mutex _writeMutex;

    std::mutex _jobsMutex;
    std::condition_variable _jobsCv;
    std::deque<IsoJob> _jobs;
    std::unordered_set<uint32_t> _unlinkedSeqs;
    std::thread _pacerThread;
};

} // namespace Service::Win
