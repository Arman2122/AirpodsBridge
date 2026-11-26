// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Protocol/HciProxyEngine.h"

#include <algorithm>
#include <cstring>

namespace Service::Protocol {

namespace {

// HCI Command opcodes.
constexpr uint16_t kCmdQosSetup = 0x0807;
constexpr uint16_t kCmdReset = 0x0C03;
constexpr uint16_t kCmdReadLocalVersion = 0x1001;
constexpr uint16_t kCmdReadBufferSize = 0x1005;
constexpr uint16_t kCmdReadBdAddr = 0x1009;

// HCI Event codes.
constexpr uint8_t kEvtConnectionComplete = 0x03;
constexpr uint8_t kEvtDisconnectionComplete = 0x05;
constexpr uint8_t kEvtEncryptionChange = 0x08;
constexpr uint8_t kEvtCommandComplete = 0x0E;
constexpr uint8_t kEvtCommandStatus = 0x0F;
constexpr uint8_t kEvtHardwareError = 0x10;
constexpr uint8_t kEvtNumCompletedPackets = 0x13;
constexpr uint8_t kEvtSyncConnectionComplete = 0x2C;

// L2CAP signaling codes.
constexpr uint8_t kL2capCmdReject = 0x01;
constexpr uint8_t kL2capConnReq = 0x02;
constexpr uint8_t kL2capConnRsp = 0x03;
constexpr uint8_t kL2capConfigReq = 0x04;
constexpr uint8_t kL2capConfigRsp = 0x05;
constexpr uint8_t kL2capDisconnReq = 0x06;
constexpr uint8_t kL2capDisconnRsp = 0x07;

constexpr uint16_t kL2capSignalingCid = 0x0001;
constexpr uint32_t kMaxOurOutstandingPerController = 2;

uint16_t ReadLe16(std::span<const uint8_t> b, size_t off)
{
    return static_cast<uint16_t>(b[off]) |
           static_cast<uint16_t>(static_cast<uint16_t>(b[off + 1]) << 8);
}

void WriteLe16(std::span<uint8_t> b, size_t off, uint16_t v)
{
    b[off] = static_cast<uint8_t>(v & 0xFFu);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
}

void WriteLe32(std::span<uint8_t> b, size_t off, uint32_t v)
{
    b[off] = static_cast<uint8_t>(v & 0xFFu);
    b[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
    b[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    b[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

void PushLe16(std::vector<uint8_t> &v, uint16_t x)
{
    v.push_back(static_cast<uint8_t>(x & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 8) & 0xFFu));
}

bool IsReservedLocalCid(uint16_t cid)
{
    return cid >= HciProxyEngine::kFirstReservedCid && cid <= HciProxyEngine::kLastReservedCid;
}

bool IsReservedSigId(uint8_t id)
{
    return id >= HciProxyEngine::kFirstReservedSigId && id <= HciProxyEngine::kLastReservedSigId;
}

std::vector<uint8_t> BuildL2capConnReq(uint8_t sigId, uint16_t psm, uint16_t scid)
{
    std::vector<uint8_t> sig;
    sig.reserve(8);
    sig.push_back(kL2capConnReq);
    sig.push_back(sigId);
    PushLe16(sig, 4);
    PushLe16(sig, psm);
    PushLe16(sig, scid);
    return sig;
}

std::vector<uint8_t>
BuildL2capConnRsp(uint8_t sigId, uint16_t dcid, uint16_t scid, uint16_t result, uint16_t status)
{
    std::vector<uint8_t> sig;
    sig.reserve(12);
    sig.push_back(kL2capConnRsp);
    sig.push_back(sigId);
    PushLe16(sig, 8);
    PushLe16(sig, dcid);
    PushLe16(sig, scid);
    PushLe16(sig, result);
    PushLe16(sig, status);
    return sig;
}

std::vector<uint8_t> BuildL2capConfigReqMtu(uint8_t sigId, uint16_t dcid, uint16_t mtu)
{
    std::vector<uint8_t> sig;
    sig.reserve(12);
    sig.push_back(kL2capConfigReq);
    sig.push_back(sigId);
    PushLe16(sig, 8);
    PushLe16(sig, dcid);
    PushLe16(sig, 0x0000); // flags = 0 (final)
    sig.push_back(0x01);   // Option type: MTU
    sig.push_back(0x02);   // Option length: 2
    PushLe16(sig, mtu);
    return sig;
}

std::vector<uint8_t>
BuildL2capConfigRsp(uint8_t sigId, uint16_t scid, uint16_t flags, uint16_t result)
{
    std::vector<uint8_t> sig;
    sig.reserve(10);
    sig.push_back(kL2capConfigRsp);
    sig.push_back(sigId);
    PushLe16(sig, 6);
    PushLe16(sig, scid);
    PushLe16(sig, flags);
    PushLe16(sig, result);
    return sig;
}

std::vector<uint8_t> BuildL2capDisconnReq(uint8_t sigId, uint16_t dcid, uint16_t scid)
{
    std::vector<uint8_t> sig;
    sig.reserve(8);
    sig.push_back(kL2capDisconnReq);
    sig.push_back(sigId);
    PushLe16(sig, 4);
    PushLe16(sig, dcid);
    PushLe16(sig, scid);
    return sig;
}

std::vector<uint8_t> BuildL2capDisconnRsp(uint8_t sigId, uint16_t dcid, uint16_t scid)
{
    std::vector<uint8_t> sig;
    sig.reserve(8);
    sig.push_back(kL2capDisconnRsp);
    sig.push_back(sigId);
    PushLe16(sig, 4);
    PushLe16(sig, dcid);
    PushLe16(sig, scid);
    return sig;
}

} // namespace

HciProxyEngine::HciProxyEngine(Callbacks callbacks) : _callbacks{std::move(callbacks)} {}

void HciProxyEngine::SetCallbacks(Callbacks callbacks)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _callbacks = std::move(callbacks);
}

void HciProxyEngine::SetConfig(const ProxyConfig &config)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _config = config;
    if (_config.pollLatencyUs < 1250 || _config.pollLatencyUs > 50000) {
        _config.pollLatencyUs = kDefaultPollLatencyUs;
    }
}

ProxyConfig HciProxyEngine::GetConfig() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    return _config;
}

void HciProxyEngine::SetDeferAapUntilAvdtp(bool defer)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _deferAapUntilAvdtp = defer;
}

void HciProxyEngine::SetPullModeForInjectedAcl(bool pullMode)
{
    std::lock_guard<std::mutex> lock{_mutex};
    _pullModeInjectedAcl = pullMode;
}

bool HciProxyEngine::PopNextInjectedAcl(std::vector<uint8_t> &outPacket, uint16_t &outHandle)
{
    outPacket.clear();
    outHandle = 0;

    std::lock_guard<std::mutex> lock{_mutex};
    while (!_ourAclQueue.empty()) {
        const auto &front = _ourAclQueue.front();
        const auto it = _aclLinks.find(front.handle);
        if (it == _aclLinks.end()) {
            _ourAclQueue.pop_front();
            continue;
        }
        if (!CanInjectOnHandleLocked(it->second)) {
            return false;
        }

        outHandle = front.handle;
        outPacket = std::move(_ourAclQueue.front().packet);
        _ourAclQueue.pop_front();
        ++it->second.ourOutstandingPackets;
        ++_totalOurOutstanding;
        ++_injectedAclPackets;
        return true;
    }
    return false;
}

void HciProxyEngine::RevertInjectedAcl(uint16_t handle)
{
    std::lock_guard<std::mutex> lock{_mutex};
    if (auto it = _aclLinks.find(handle);
        it != _aclLinks.end() && it->second.ourOutstandingPackets > 0)
    {
        --it->second.ourOutstandingPackets;
    }
    if (_totalOurOutstanding > 0) {
        --_totalOurOutstanding;
    }
}

void HciProxyEngine::Tick(uint64_t nowMs)
{
    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _lastTickMs = nowMs;

        for (auto &[handle, link] : _aclLinks) {
            if (!IsTrackedAirPodsAddressLocked(link.btAddress)) {
                continue;
            }
            if (link.encrypted && link.aap.state == AapState::WaitingForLink) {
                if (link.avdtp.signalOpen ||
                    (nowMs >= link.aap.stateEnteredMs && nowMs - link.aap.stateEnteredMs >= 3500u))
                {
                    BeginAapConnectLocked(link, deferred);
                }
            }
            else if (
                (link.aap.state == AapState::Connecting ||
                 link.aap.state == AapState::Configuring) &&
                nowMs >= link.aap.stateEnteredMs && nowMs - link.aap.stateEnteredMs >= 3000u)
            {
                const uint8_t retries = link.aap.retryCount;
                const uint8_t maxRetries = link.avdtp.signalOpen ? 3u : 1u;
                deferred.logs.emplace_back(
                    LogLevel::Warn, "Hidden L2CAP AAP handshake stalled (state=" +
                                        std::to_string(static_cast<int>(link.aap.state)) +
                                        ", scid=" + std::to_string(link.aap.localCid) +
                                        ", avdtp=" + (link.avdtp.signalOpen ? "1" : "0") +
                                        ", outstanding=" + std::to_string(_totalOurOutstanding) +
                                        ", winPduRem=" + std::to_string(link.windowsPduRemaining) +
                                        ", attempt=" + std::to_string(retries + 1) + ").");
                if (retries < maxRetries && _totalOurOutstanding < kMaxOurOutstandingPerController)
                {
                    link.aap.retryCount = static_cast<uint8_t>(retries + 1);
                    BeginAapConnectLocked(link, deferred);
                }
                else if (retries >= maxRetries) {
                    link.aap = AapChannel{};
                    // If AVDTP is not up yet, return to WaitingForLink so AAP opens immediately
                    // the moment AVDTP comes online instead of giving up permanently.
                    if (!link.avdtp.signalOpen) {
                        link.aap.state = AapState::WaitingForLink;
                        link.aap.stateEnteredMs = nowMs + 60000u;
                    }
                    deferred.aapStates.emplace_back(link.btAddress, false, 0xFFFFu);
                }
                else {
                    // Controller has not acknowledged earlier packets yet; bump timer so we
                    // don't spam retries or flood the queue while the baseband is congested.
                    link.aap.stateEnteredMs = nowMs;
                }
            }
        }
        FlushQueuedAclLocked(deferred);
    }
    DispatchDeferred(deferred);
}

void HciProxyEngine::Reset(std::string_view reason)
{
    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        for (const auto &[handle, link] : _aclLinks) {
            if (link.aap.state == AapState::Open) {
                deferred.aapStates.emplace_back(link.btAddress, false, 0u);
            }
        }
        if (!_scoLinks.empty()) {
            deferred.callAudioStates.emplace_back(0ull, false);
        }

        _aclLinks.clear();
        _scoLinks.clear();
        _ourAclQueue.clear();
        _eventStreamBuf.clear();
        _aclInStreamBuf.clear();
        _totalOurOutstanding = 0;
        _ownPendingCommandOpcode = 0;
        _aclFramed = false;
        deferred.logs.emplace_back(LogLevel::Info, "HCI proxy reset: " + std::string{reason});
    }
    DispatchDeferred(deferred);
}

void HciProxyEngine::OpenAapChannel(uint64_t btAddress)
{
    if (btAddress == 0) {
        return;
    }

    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _wantedAapAddresses[btAddress] = true;

        if (auto *link = FindLinkByAddressLocked(btAddress); link != nullptr) {
            if (link->aap.state == AapState::Open) {
                deferred.aapStates.emplace_back(btAddress, true, 0u);
            }
            else if (
                link->aap.state == AapState::Idle || link->aap.state == AapState::WaitingForLink)
            {
                if (link->encrypted && (!_deferAapUntilAvdtp || link->avdtp.signalOpen)) {
                    BeginAapConnectLocked(*link, deferred);
                }
                else {
                    link->aap.state = AapState::WaitingForLink;
                    link->aap.stateEnteredMs = _lastTickMs;
                }
            }
            else if (
                link->aap.state == AapState::Connecting &&
                _lastTickMs >= link->aap.stateEnteredMs &&
                _lastTickMs - link->aap.stateEnteredMs >= 2500u)
            {
                BeginAapConnectLocked(*link, deferred);
            }
        }
    }
    DispatchDeferred(deferred);
}

void HciProxyEngine::CloseAapChannel(uint64_t btAddress)
{
    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _wantedAapAddresses.erase(btAddress);

        if (auto *link = FindLinkByAddressLocked(btAddress); link != nullptr) {
            const bool wasOpen = link->aap.state == AapState::Open;
            if ((link->aap.state == AapState::Open || link->aap.state == AapState::Configuring) &&
                link->aap.remoteCid != 0 && link->aap.localCid != 0)
            {
                const uint8_t sigId = AllocateSigIdLocked();
                link->aap.pendingSigId = sigId;
                link->aap.state = AapState::Disconnecting;
                const auto req =
                    BuildL2capDisconnReq(sigId, link->aap.remoteCid, link->aap.localCid);
                QueueL2capPduLocked(link->handle, kL2capSignalingCid, req, deferred);
            }
            else {
                link->aap = AapChannel{};
            }
            if (wasOpen) {
                deferred.aapStates.emplace_back(btAddress, false, 0u);
            }
        }
    }
    DispatchDeferred(deferred);
}

bool HciProxyEngine::SendAapPacket(uint64_t btAddress, std::span<const uint8_t> payload)
{
    if (btAddress == 0 || payload.empty() || payload.size() > kMaxAapPacketBytes) {
        return false;
    }

    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        auto *link = FindLinkByAddressLocked(btAddress);
        if (link == nullptr || link->aap.state != AapState::Open || link->aap.remoteCid == 0) {
            return false;
        }
        QueueL2capPduLocked(link->handle, link->aap.remoteCid, payload, deferred);
    }
    DispatchDeferred(deferred);
    return true;
}

bool HciProxyEngine::ProcessWindowsCommand(std::vector<uint8_t> &command)
{
    if (command.size() < 3) {
        return true;
    }

    const uint16_t opcode = ReadLe16(command, 0);
    std::lock_guard<std::mutex> lock{_mutex};

    if (!_config.lowLatencyQos) {
        return true;
    }

    // Rewrite Windows' default 25 ms ACL poll latency down to our configured low-latency target
    // (default 11.25 ms / 18 slots — 55% faster than stock Windows while leaving 12 free slots
    // after each 3-DH5 A2DP packet for AirPods' single-radio primary-to-secondary inter-bud
    // relay) while preserving Windows' spec-valid `Service_Type` and `Delay_Variation`.
    if (opcode == kCmdQosSetup && command.size() >= 23) {
        const uint16_t handle = static_cast<uint16_t>(ReadLe16(command, 3) & 0x0FFFu);
        if (const auto it = _aclLinks.find(handle);
            it != _aclLinks.end() && IsTrackedAirPodsAddressLocked(it->second.btAddress))
        {
            WriteLe32(command, 15, _config.pollLatencyUs);
            ++_rewrittenQosCommands;
        }
    }
    return true;
}

void HciProxyEngine::ProcessWindowsAclOut(
    std::span<const uint8_t> bulkOut, std::vector<uint8_t> &forwardToController)
{
    forwardToController.clear();
    if (bulkOut.empty()) {
        return;
    }

    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        // Forward all Windows ACL packets intact while tracking L2CAP PDU boundaries and snooping
        // AVDTP/RFCOMM channel setup.
        forwardToController.assign(bulkOut.begin(), bulkOut.end());

        size_t pos = 0;
        while (pos + 4 <= bulkOut.size()) {
            const uint16_t hdr = ReadLe16(bulkOut, pos);
            const uint16_t handle = static_cast<uint16_t>(hdr & 0x0FFFu);
            const uint8_t pbFlag = static_cast<uint8_t>((hdr >> 12) & 0x03u);
            const uint16_t aclLen = ReadLe16(bulkOut, pos + 2);
            if (pos + 4 + aclLen > bulkOut.size()) {
                break;
            }

            if (auto it = _aclLinks.find(handle); it != _aclLinks.end()) {
                auto &link = it->second;
                const auto payload = bulkOut.subspan(pos + 4, aclLen);
                if (pbFlag == 0x01) {
                    if (link.windowsPduRemaining > aclLen) {
                        link.windowsPduRemaining -= aclLen;
                    }
                    else {
                        link.windowsPduRemaining = 0;
                    }
                }
                else {
                    if (payload.size() >= 4) {
                        const uint16_t l2capLen = ReadLe16(payload, 0);
                        const uint32_t totalPdu = 4u + static_cast<uint32_t>(l2capLen);
                        link.windowsPduRemaining = totalPdu > aclLen ? (totalPdu - aclLen) : 0u;
                    }
                    else {
                        link.windowsPduRemaining = 0;
                    }
                }
                SnoopWindowsAclPacketLocked(link, pbFlag, payload, deferred);
            }

            pos += 4 + aclLen;
        }

        // Now that any multi-fragment Windows PDU may have finished, flush queued service ACLs.
        FlushQueuedAclLocked(deferred);
    }
    DispatchDeferred(deferred);
}

void HciProxyEngine::ProcessControllerEvents(
    std::span<const uint8_t> interruptIn, std::vector<uint8_t> &forwardToWindows)
{
    forwardToWindows.clear();
    if (interruptIn.empty()) {
        return;
    }

    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        _eventStreamBuf.insert(_eventStreamBuf.end(), interruptIn.begin(), interruptIn.end());

        size_t consumed = 0;
        while (consumed + 2 <= _eventStreamBuf.size()) {
            const uint8_t paramLen = _eventStreamBuf[consumed + 1];
            const size_t totalLen = 2u + static_cast<size_t>(paramLen);
            if (consumed + totalLen > _eventStreamBuf.size()) {
                break;
            }

            const std::span<const uint8_t> evt{_eventStreamBuf.data() + consumed, totalLen};
            HandleSingleEventLocked(evt, forwardToWindows, deferred);
            consumed += totalLen;
        }

        if (consumed > 0) {
            _eventStreamBuf.erase(
                _eventStreamBuf.begin(),
                _eventStreamBuf.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
    }
    DispatchDeferred(deferred);
}

void HciProxyEngine::ProcessControllerAclIn(
    std::span<const uint8_t> bulkIn, std::vector<uint8_t> &forwardToWindows)
{
    forwardToWindows.clear();
    if (bulkIn.empty()) {
        return;
    }

    DeferredNotifications deferred;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        if (!_aclFramed) {
            // Before the first HCI_Reset completes (e.g. during vendor firmware download on Intel
            // or Realtek chips), Bulk IN carries vendor boot responses, not standard HCI ACL.
            forwardToWindows.assign(bulkIn.begin(), bulkIn.end());
            return;
        }

        _aclInStreamBuf.insert(_aclInStreamBuf.end(), bulkIn.begin(), bulkIn.end());

        size_t consumed = 0;
        while (consumed + 4 <= _aclInStreamBuf.size()) {
            const uint16_t aclLen = ReadLe16(_aclInStreamBuf, consumed + 2);
            const size_t totalLen = 4u + static_cast<size_t>(aclLen);
            if (consumed + totalLen > _aclInStreamBuf.size()) {
                break;
            }

            const std::span<const uint8_t> pkt{_aclInStreamBuf.data() + consumed, totalLen};
            HandleControllerAclPacketLocked(pkt, forwardToWindows, deferred);
            consumed += totalLen;
        }

        if (consumed > 0) {
            _aclInStreamBuf.erase(
                _aclInStreamBuf.begin(),
                _aclInStreamBuf.begin() + static_cast<std::ptrdiff_t>(consumed));
        }
    }
    DispatchDeferred(deferred);
}

HciProxyEngine::Telemetry HciProxyEngine::GetTelemetry() const
{
    std::lock_guard<std::mutex> lock{_mutex};
    Telemetry t{};
    t.hciManufacturer = _hciManufacturer;
    t.localAddress = _localAddress;
    t.aclMaxLength = _aclMaxLength;
    t.aclTotalBuffers = _aclTotalBuffers;
    t.activeAclLinks = static_cast<uint32_t>(_aclLinks.size());
    for (const auto &[handle, link] : _aclLinks) {
        if (link.aap.state == AapState::Open) {
            ++t.activeAapChannels;
        }
    }
    t.callAudioActive = !_scoLinks.empty();
    t.injectedAclPackets = _injectedAclPackets;
    t.interceptedAclPackets = _interceptedAclPackets;
    t.rewrittenQosCommands = _rewrittenQosCommands;
    t.declinedHfpRequests = _declinedHfpRequests;
    return t;
}

bool HciProxyEngine::IsAapChannelOpen(uint64_t btAddress) const
{
    std::lock_guard<std::mutex> lock{_mutex};
    const auto *link = FindLinkByAddressLocked(btAddress);
    return link != nullptr && link->aap.state == AapState::Open;
}

void HciProxyEngine::DispatchDeferred(DeferredNotifications &deferred)
{
    Callbacks cb;
    {
        std::lock_guard<std::mutex> lock{_mutex};
        cb = _callbacks;
    }

    for (auto &[lvl, msg] : deferred.logs) {
        if (cb.onLog) {
            cb.onLog(lvl, std::move(msg));
        }
    }
    for (auto &cmd : deferred.cmdToSend) {
        if (cb.sendCommandToController) {
            cb.sendCommandToController(std::move(cmd));
        }
    }
    for (auto &acl : deferred.aclToSend) {
        if (cb.sendAclToController) {
            cb.sendAclToController(std::move(acl));
        }
    }
    if (deferred.notifyInjectedAcl && cb.onInjectedAclAvailable) {
        cb.onInjectedAclAvailable();
    }
    for (auto &[addr, connected, err] : deferred.aapStates) {
        if (cb.onAapConnectionChanged) {
            cb.onAapConnectionChanged(addr, connected, err);
        }
    }
    for (auto &[addr, pkt] : deferred.aapPackets) {
        if (cb.onAapPacketReceived) {
            cb.onAapPacketReceived(addr, std::move(pkt));
        }
    }
    for (auto &[addr, active] : deferred.callAudioStates) {
        if (cb.onCallAudioChanged) {
            cb.onCallAudioChanged(addr, active);
        }
    }
    for (auto &trouble : deferred.troubles) {
        if (cb.onControllerTrouble) {
            cb.onControllerTrouble(std::move(trouble));
        }
    }
}

uint16_t HciProxyEngine::AllocateLocalCidLocked()
{
    const uint16_t cid = _nextLocalCid;
    _nextLocalCid = (_nextLocalCid >= kLastReservedCid) ? kFirstReservedCid
                                                        : static_cast<uint16_t>(_nextLocalCid + 1);
    return cid;
}

uint8_t HciProxyEngine::AllocateSigIdLocked()
{
    const uint8_t id = _nextSigId;
    _nextSigId = (_nextSigId >= kLastReservedSigId) ? kFirstReservedSigId
                                                    : static_cast<uint8_t>(_nextSigId + 1);
    return id;
}

HciProxyEngine::AclLink *HciProxyEngine::FindLinkByAddressLocked(uint64_t btAddress)
{
    for (auto &[handle, link] : _aclLinks) {
        if (link.btAddress == btAddress) {
            return &link;
        }
    }
    return nullptr;
}

const HciProxyEngine::AclLink *HciProxyEngine::FindLinkByAddressLocked(uint64_t btAddress) const
{
    for (const auto &[handle, link] : _aclLinks) {
        if (link.btAddress == btAddress) {
            return &link;
        }
    }
    return nullptr;
}

bool HciProxyEngine::IsTrackedAirPodsAddressLocked(uint64_t btAddress) const
{
    return _wantedAapAddresses.find(btAddress) != _wantedAapAddresses.end();
}

void HciProxyEngine::BeginAapConnectLocked(AclLink &link, DeferredNotifications &deferred)
{
    // Drop any stale unsent AAP packets queued for this handle so a retry never stacks behind
    // an older stalled attempt.
    _ourAclQueue.erase(
        std::remove_if(
            _ourAclQueue.begin(), _ourAclQueue.end(),
            [h = link.handle](const QueuedAclPacket &p) { return p.handle == h; }),
        _ourAclQueue.end());

    const uint8_t prevRetries = link.aap.retryCount;
    link.aap = AapChannel{};
    link.aap.state = AapState::Connecting;
    link.aap.retryCount = prevRetries;
    link.aap.stateEnteredMs = _lastTickMs;
    link.aap.localCid = AllocateLocalCidLocked();
    link.aap.pendingSigId = AllocateSigIdLocked();

    const auto connReq = BuildL2capConnReq(link.aap.pendingSigId, kAapPsm, link.aap.localCid);
    QueueL2capPduLocked(link.handle, kL2capSignalingCid, connReq, deferred);
    deferred.logs.emplace_back(
        LogLevel::Info, "Initiating hidden L2CAP AAP connection (PSM 0x1001, handle=" +
                            std::to_string(link.handle) +
                            ", scid=" + std::to_string(link.aap.localCid) +
                            ", sigId=" + std::to_string(link.aap.pendingSigId) +
                            ", avdtp=" + (link.avdtp.signalOpen ? "1" : "0") +
                            ", queued=" + std::to_string(_ourAclQueue.size()) +
                            ", outstanding=" + std::to_string(_totalOurOutstanding) +
                            ", attempt=" + std::to_string(prevRetries + 1) + ").");
}

void HciProxyEngine::QueueL2capPduLocked(
    uint16_t handle, uint16_t remoteCid, std::span<const uint8_t> payload,
    DeferredNotifications &deferred)
{
    std::vector<uint8_t> pdu;
    pdu.reserve(4 + payload.size());
    PushLe16(pdu, static_cast<uint16_t>(payload.size()));
    PushLe16(pdu, remoteCid);
    pdu.insert(pdu.end(), payload.begin(), payload.end());

    const size_t maxFrag = std::max<size_t>(27, _aclMaxLength);
    size_t offset = 0;
    while (offset < pdu.size()) {
        const size_t chunk = std::min(maxFrag, pdu.size() - offset);
        const uint8_t pbFlag = (offset == 0) ? 0x02 : 0x01;

        std::vector<uint8_t> aclPkt;
        AppendRawAclFragment(
            aclPkt, handle, pbFlag, std::span<const uint8_t>{pdu.data() + offset, chunk});
        _ourAclQueue.push_back(QueuedAclPacket{.handle = handle, .packet = std::move(aclPkt)});
        offset += chunk;
    }

    FlushQueuedAclLocked(deferred);
}

bool HciProxyEngine::CanInjectOnHandleLocked(const AclLink &link) const
{
    // Never interleave between fragments of a Windows L2CAP PDU on this ACL handle.
    if (link.windowsPduRemaining > 0) {
        return false;
    }
    // Only ever occupy the disjoint hardware ACL buffers we reserved from Windows in
    // `HCI_Read_Buffer_Size`, guaranteeing `windowsInFlight + ourInFlight <= _aclTotalBuffers`.
    const uint32_t maxSlots =
        _reservedAclBuffers > 0 ? static_cast<uint32_t>(_reservedAclBuffers) : 1u;
    return _totalOurOutstanding < maxSlots;
}

void HciProxyEngine::FlushQueuedAclLocked(DeferredNotifications &deferred)
{
    while (!_ourAclQueue.empty()) {
        const auto &front = _ourAclQueue.front();
        const auto it = _aclLinks.find(front.handle);
        if (it == _aclLinks.end()) {
            _ourAclQueue.pop_front();
            continue;
        }
        if (!CanInjectOnHandleLocked(it->second)) {
            break;
        }

        if (_pullModeInjectedAcl) {
            // Wake the single Bulk OUT pipe worker so it pulls right before `WinUsb_WritePipe`
            // after checking `windowsPduRemaining == 0` on its own thread.
            deferred.notifyInjectedAcl = true;
            break;
        }

        deferred.aclToSend.push_back(std::move(_ourAclQueue.front().packet));
        _ourAclQueue.pop_front();
        ++it->second.ourOutstandingPackets;
        ++_totalOurOutstanding;
        ++_injectedAclPackets;
    }
}

void HciProxyEngine::HandleSingleEventLocked(
    std::span<const uint8_t> evt, std::vector<uint8_t> &forwardToWindows,
    DeferredNotifications &deferred)
{
    const uint8_t code = evt[0];

    switch (code) {
    case kEvtNumCompletedPackets:
        HandleNumCompletedPacketsLocked(evt, forwardToWindows, deferred);
        return;

    case kEvtCommandComplete: {
        if (evt.size() >= 6) {
            const uint16_t opcode = ReadLe16(evt, 3);
            const uint8_t status = evt[5];

            if (_ownPendingCommandOpcode != 0 && opcode == _ownPendingCommandOpcode) {
                _ownPendingCommandOpcode = 0;
                return; // Hide completion of our own injected HCI command.
            }

            if (opcode == kCmdReset && status == 0) {
                _aclFramed = true;
                _aclLinks.clear();
                _scoLinks.clear();
                _ourAclQueue.clear();
                _totalOurOutstanding = 0;
            }
            else if (opcode == kCmdReadLocalVersion && status == 0 && evt.size() >= 14) {
                const uint16_t mfg = ReadLe16(evt, 10);
                std::vector<uint8_t> ver(evt.begin() + 6, evt.begin() + 14);
                if (!_lastLocalVersion.empty() && _lastLocalVersion != ver) {
                    deferred.troubles.emplace_back(
                        "Controller local version changed unexpectedly (firmware reload or loss).");
                }
                _lastLocalVersion = std::move(ver);
                _hciManufacturer = mfg;
            }
            else if (opcode == kCmdReadBufferSize && status == 0 && evt.size() >= 11) {
                const uint16_t maxLen = ReadLe16(evt, 6);
                const uint16_t numBufs = ReadLe16(evt, 9);
                if (maxLen >= 27) {
                    _aclMaxLength = maxLen;
                }
                _aclTotalBuffers = numBufs;
                // Reserve disjoint hardware ACL buffer(s) exclusively for our hidden AAP channel
                // by subtracting them from `HC_Total_Num_ACL_Data_Packets` reported to Windows.
                // Reserve 1 slot on compact 3..5-buffer controllers (e.g. Intel BE200 has 4) so
                // Windows keeps 3 full A2DP audio buffers (+50% headroom) while guaranteeing
                // `windowsInFlight + ourInFlight <= numBufs`; reserve 2 slots when `numBufs >= 6`.
                _reservedAclBuffers = numBufs >= 6
                                          ? static_cast<uint16_t>(kMaxOurOutstandingPerController)
                                          : (numBufs >= 3 ? uint16_t{1} : uint16_t{0});
                const uint16_t winBufs = static_cast<uint16_t>(numBufs - _reservedAclBuffers);
                deferred.logs.emplace_back(
                    LogLevel::Info, "HCI Read_Buffer_Size: aclMaxLen=" + std::to_string(maxLen) +
                                        ", hwAclBufs=" + std::to_string(numBufs) +
                                        ", reservedForAap=" + std::to_string(_reservedAclBuffers) +
                                        ", windowsBufs=" + std::to_string(winBufs));
                if (_reservedAclBuffers > 0) {
                    std::vector<uint8_t> rewritten(evt.begin(), evt.end());
                    WriteLe16(rewritten, 9, winBufs);
                    forwardToWindows.insert(
                        forwardToWindows.end(), rewritten.begin(), rewritten.end());
                    return;
                }
            }
            else if (opcode == kCmdReadBdAddr && status == 0 && evt.size() >= 12) {
                _localAddress = ReadBdAddrLe(evt.subspan(6, 6));
            }
        }
        break;
    }

    case kEvtCommandStatus: {
        if (evt.size() >= 6) {
            const uint16_t opcode = ReadLe16(evt, 4);
            if (_ownPendingCommandOpcode != 0 && opcode == _ownPendingCommandOpcode) {
                _ownPendingCommandOpcode = 0;
                return; // Hide status of our own injected HCI command.
            }
        }
        break;
    }

    case kEvtConnectionComplete: {
        if (evt.size() >= 13 && evt[2] == 0) {
            const uint16_t handle = static_cast<uint16_t>(ReadLe16(evt, 3) & 0x0FFFu);
            const uint64_t addr = ReadBdAddrLe(evt.subspan(5, 6));
            const uint8_t linkType = evt[11];
            const bool encrypted = evt[12] != 0;
            if (linkType == 0x01) { // ACL
                AclLink link{};
                link.handle = handle;
                link.btAddress = addr;
                link.encrypted = encrypted;
                auto [it, inserted] = _aclLinks.insert_or_assign(handle, std::move(link));
                if (IsTrackedAirPodsAddressLocked(addr)) {
                    deferred.logs.emplace_back(
                        LogLevel::Info,
                        "Tracked AirPods ACL connected (handle=" + std::to_string(handle) +
                            ", encrypted=" + (encrypted ? "1" : "0") + ").");
                    if (it->second.encrypted && !_deferAapUntilAvdtp) {
                        BeginAapConnectLocked(it->second, deferred);
                    }
                    else {
                        it->second.aap.state = AapState::WaitingForLink;
                        it->second.aap.stateEnteredMs = _lastTickMs;
                    }
                }
            }
        }
        break;
    }

    case kEvtEncryptionChange: {
        if (evt.size() >= 6 && evt[2] == 0) {
            const uint16_t handle = static_cast<uint16_t>(ReadLe16(evt, 3) & 0x0FFFu);
            const bool encrypted = evt[5] != 0;
            if (auto it = _aclLinks.find(handle); it != _aclLinks.end()) {
                it->second.encrypted = encrypted;
                if (encrypted && IsTrackedAirPodsAddressLocked(it->second.btAddress) &&
                    (it->second.aap.state == AapState::Idle ||
                     it->second.aap.state == AapState::WaitingForLink))
                {
                    if (!_deferAapUntilAvdtp || it->second.avdtp.signalOpen) {
                        BeginAapConnectLocked(it->second, deferred);
                    }
                    else {
                        it->second.aap.state = AapState::WaitingForLink;
                        it->second.aap.stateEnteredMs = _lastTickMs;
                        deferred.logs.emplace_back(
                            LogLevel::Info,
                            "Tracked AirPods ACL encrypted (handle=" + std::to_string(handle) +
                                "); waiting for AVDTP before opening AAP.");
                    }
                }
            }
        }
        break;
    }

    case kEvtSyncConnectionComplete: {
        if (evt.size() >= 12 && evt[2] == 0) {
            const uint16_t handle = static_cast<uint16_t>(ReadLe16(evt, 3) & 0x0FFFu);
            const uint64_t addr = ReadBdAddrLe(evt.subspan(5, 6));
            const bool wasEmpty = _scoLinks.empty();
            _scoLinks[handle] = addr;
            if (wasEmpty) {
                deferred.callAudioStates.emplace_back(addr, true);
            }
        }
        break;
    }

    case kEvtDisconnectionComplete: {
        if (evt.size() >= 6 && evt[2] == 0) {
            const uint16_t handle = static_cast<uint16_t>(ReadLe16(evt, 3) & 0x0FFFu);
            const uint8_t reason = evt[5];
            if (auto scoIt = _scoLinks.find(handle); scoIt != _scoLinks.end()) {
                const uint64_t addr = scoIt->second;
                _scoLinks.erase(scoIt);
                if (_scoLinks.empty()) {
                    deferred.callAudioStates.emplace_back(addr, false);
                }
            }
            if (auto aclIt = _aclLinks.find(handle); aclIt != _aclLinks.end()) {
                if (IsTrackedAirPodsAddressLocked(aclIt->second.btAddress)) {
                    deferred.logs.emplace_back(
                        LogLevel::Info,
                        "Tracked AirPods ACL disconnected (handle=" + std::to_string(handle) +
                            ", reason=" + std::to_string(reason) + ", aapState=" +
                            std::to_string(static_cast<int>(aclIt->second.aap.state)) + ").");
                }
                if (aclIt->second.ourOutstandingPackets <= _totalOurOutstanding) {
                    _totalOurOutstanding -= aclIt->second.ourOutstandingPackets;
                }
                else {
                    _totalOurOutstanding = 0;
                }
                if (aclIt->second.aap.state == AapState::Open) {
                    deferred.aapStates.emplace_back(aclIt->second.btAddress, false, 0u);
                }
                _aclLinks.erase(aclIt);
                FlushQueuedAclLocked(deferred);
            }
        }
        break;
    }

    case kEvtHardwareError: {
        const uint8_t hwCode = evt.size() >= 3 ? evt[2] : 0;
        deferred.troubles.emplace_back(
            "Bluetooth controller reported HCI Hardware_Error (code " + std::to_string(hwCode) +
            ").");
        break;
    }

    default:
        break;
    }

    forwardToWindows.insert(forwardToWindows.end(), evt.begin(), evt.end());
}

void HciProxyEngine::HandleNumCompletedPacketsLocked(
    std::span<const uint8_t> evt, std::vector<uint8_t> &forwardToWindows,
    DeferredNotifications &deferred)
{
    // Bluetooth Core Spec Vol 4 Part E 7.7.19:
    //   [0]=0x13, [1]=param_len, [2]=num_handles (N),
    //   [3 .. 3+2N-1] = Connection_Handle[0..N-1],
    //   [3+2N .. 3+4N-1] = Num_Completed_Packets[0..N-1].
    if (evt.size() < 3) {
        forwardToWindows.insert(forwardToWindows.end(), evt.begin(), evt.end());
        return;
    }

    const size_t numHandles = evt[2];
    if (evt.size() < 3 + numHandles * 4) {
        forwardToWindows.insert(forwardToWindows.end(), evt.begin(), evt.end());
        return;
    }

    std::vector<uint16_t> keptHandles;
    std::vector<uint16_t> keptCounts;
    keptHandles.reserve(numHandles);
    keptCounts.reserve(numHandles);

    for (size_t i = 0; i < numHandles; ++i) {
        const uint16_t rawHandle = ReadLe16(evt, 3 + i * 2);
        const uint16_t handle = static_cast<uint16_t>(rawHandle & 0x0FFFu);
        uint16_t completed = ReadLe16(evt, 3 + numHandles * 2 + i * 2);

        if (auto it = _aclLinks.find(handle);
            it != _aclLinks.end() && it->second.ourOutstandingPackets > 0 && completed > 0)
        {
            const uint16_t claim = static_cast<uint16_t>(
                std::min<uint32_t>(completed, it->second.ourOutstandingPackets));
            it->second.ourOutstandingPackets -= claim;
            if (claim <= _totalOurOutstanding) {
                _totalOurOutstanding -= claim;
            }
            else {
                _totalOurOutstanding = 0;
            }
            completed = static_cast<uint16_t>(completed - claim);
        }

        if (completed > 0) {
            keptHandles.push_back(rawHandle);
            keptCounts.push_back(completed);
        }
    }

    FlushQueuedAclLocked(deferred);

    if (keptHandles.empty()) {
        return; // All completions belonged to injected service ACL packets.
    }

    const size_t outN = keptHandles.size();
    forwardToWindows.push_back(kEvtNumCompletedPackets);
    forwardToWindows.push_back(static_cast<uint8_t>(1 + outN * 4));
    forwardToWindows.push_back(static_cast<uint8_t>(outN));
    for (uint16_t h : keptHandles) {
        PushLe16(forwardToWindows, h);
    }
    for (uint16_t c : keptCounts) {
        PushLe16(forwardToWindows, c);
    }
}

void HciProxyEngine::HandleControllerAclPacketLocked(
    std::span<const uint8_t> pkt, std::vector<uint8_t> &forwardToWindows,
    DeferredNotifications &deferred)
{
    const uint16_t hdr = ReadLe16(pkt, 0);
    const uint16_t handle = static_cast<uint16_t>(hdr & 0x0FFFu);
    const uint8_t pbFlag = static_cast<uint8_t>((hdr >> 12) & 0x03u);
    const std::span<const uint8_t> payload = pkt.subspan(4);

    auto it = _aclLinks.find(handle);
    if (it == _aclLinks.end()) {
        forwardToWindows.insert(forwardToWindows.end(), pkt.begin(), pkt.end());
        return;
    }

    AclLink &link = it->second;

    if (pbFlag == 0x01) { // Continuing fragment
        if (!link.rxIntercepting) {
            forwardToWindows.insert(forwardToWindows.end(), pkt.begin(), pkt.end());
            return;
        }
        ++_interceptedAclPackets;

        if (link.rxActiveLocalCid == kL2capSignalingCid) {
            link.rxSignalingBuf.insert(link.rxSignalingBuf.end(), payload.begin(), payload.end());
            if (link.rxSignalingBuf.size() >= link.rxActiveExpectedLen) {
                const auto pdu =
                    std::span<const uint8_t>{link.rxSignalingBuf.data(), link.rxActiveExpectedLen};
                HandleControllerSignalingPduLocked(link, pdu, forwardToWindows, deferred);
                link.rxSignalingBuf.clear();
                link.rxIntercepting = false;
            }
            return;
        }

        if (link.aap.localCid != 0 && link.rxActiveLocalCid == link.aap.localCid) {
            link.aap.rxReassembly.insert(
                link.aap.rxReassembly.end(), payload.begin(), payload.end());
            if (link.aap.rxReassembly.size() >= link.aap.rxExpectedLen) {
                const auto sdu =
                    std::span<const uint8_t>{link.aap.rxReassembly.data(), link.aap.rxExpectedLen};
                DeliverAapSduLocked(link, sdu, deferred);
                link.aap.rxReassembly.clear();
                link.aap.rxExpectedLen = 0;
                link.rxIntercepting = false;
            }
            return;
        }

        link.rxIntercepting = false;
        return;
    }

    // Start of a new L2CAP PDU (PB = 0x02 or 0x00).
    link.rxIntercepting = false;
    link.rxSignalingBuf.clear();

    if (payload.size() < 4) {
        forwardToWindows.insert(forwardToWindows.end(), pkt.begin(), pkt.end());
        return;
    }

    const uint16_t l2capLen = ReadLe16(payload, 0);
    const uint16_t l2capCid = ReadLe16(payload, 2);
    const auto l2capBody = payload.subspan(4);

    if (l2capCid == kL2capSignalingCid) {
        if (l2capBody.size() < l2capLen) {
            // Reassemble fragmented signaling PDU before splitting hidden vs Windows commands.
            link.rxIntercepting = true;
            link.rxActiveLocalCid = kL2capSignalingCid;
            link.rxActiveExpectedLen = l2capLen;
            link.rxSignalingBuf.assign(l2capBody.begin(), l2capBody.end());
            return;
        }
        HandleControllerSignalingPduLocked(
            link, l2capBody.subspan(0, l2capLen), forwardToWindows, deferred);
        return;
    }

    if (IsReservedLocalCid(l2capCid)) {
        ++_interceptedAclPackets;
        if (link.aap.localCid == l2capCid) {
            if (l2capBody.size() >= l2capLen) {
                DeliverAapSduLocked(link, l2capBody.subspan(0, l2capLen), deferred);
            }
            else {
                link.rxIntercepting = true;
                link.rxActiveLocalCid = l2capCid;
                link.aap.rxExpectedLen = l2capLen;
                link.aap.rxReassembly.assign(l2capBody.begin(), l2capBody.end());
            }
        }
        return;
    }

    forwardToWindows.insert(forwardToWindows.end(), pkt.begin(), pkt.end());
}

void HciProxyEngine::HandleControllerSignalingPduLocked(
    AclLink &link, std::span<const uint8_t> sigPdu, std::vector<uint8_t> &forwardToWindows,
    DeferredNotifications &deferred)
{
    std::vector<uint8_t> keptSignaling;
    keptSignaling.reserve(sigPdu.size());

    size_t pos = 0;
    while (pos + 4 <= sigPdu.size()) {
        const uint8_t code = sigPdu[pos];
        const uint8_t id = sigPdu[pos + 1];
        const uint16_t len = ReadLe16(sigPdu, pos + 2);
        if (pos + 4 + len > sigPdu.size()) {
            keptSignaling.insert(
                keptSignaling.end(), sigPdu.begin() + static_cast<std::ptrdiff_t>(pos),
                sigPdu.end());
            break;
        }

        const auto cmdBytes = sigPdu.subspan(pos, 4 + len);
        const auto data = sigPdu.subspan(pos + 4, len);
        bool consume = false;

        // 1. Responses to our reserved signaling IDs (0xC0..0xFE).
        if (IsReservedSigId(id) && (code == kL2capConnRsp || code == kL2capConfigRsp ||
                                    code == kL2capDisconnRsp || code == kL2capCmdReject))
        {
            consume = true;
            ++_interceptedAclPackets;

            if (code == kL2capConnRsp && len >= 8 && link.aap.state == AapState::Connecting &&
                id == link.aap.pendingSigId)
            {
                const uint16_t dcid = ReadLe16(data, 0);
                const uint16_t scid = ReadLe16(data, 2);
                const uint16_t result = ReadLe16(data, 4);
                if (scid == link.aap.localCid) {
                    if (result == 0x0000) { // Connection successful
                        link.aap.remoteCid = dcid;
                        link.aap.state = AapState::Configuring;
                        link.aap.stateEnteredMs = _lastTickMs;
                        link.aap.pendingSigId = AllocateSigIdLocked();
                        const auto cfgReq =
                            BuildL2capConfigReqMtu(link.aap.pendingSigId, dcid, kOurAapMtu);
                        QueueL2capPduLocked(link.handle, kL2capSignalingCid, cfgReq, deferred);
                    }
                    else if (result == 0x0001) {
                        // Connection pending: wait for subsequent ConnRsp with final result.
                        link.aap.stateEnteredMs = _lastTickMs;
                        deferred.logs.emplace_back(
                            LogLevel::Info, "Hidden L2CAP AAP ConnRsp pending (status=" +
                                                std::to_string(ReadLe16(data, 6)) + ").");
                    }
                    else {
                        const uint8_t retries = link.aap.retryCount;
                        deferred.logs.emplace_back(
                            LogLevel::Warn,
                            "Hidden L2CAP AAP ConnRsp failed (result=" + std::to_string(result) +
                                ", status=" + std::to_string(ReadLe16(data, 6)) +
                                ", attempt=" + std::to_string(retries + 1) + ").");
                        if ((result == 0x0003 || result == 0x0004) && retries < 2) {
                            link.aap.retryCount = static_cast<uint8_t>(retries + 1);
                            BeginAapConnectLocked(link, deferred);
                        }
                        else {
                            link.aap = AapChannel{};
                            deferred.aapStates.emplace_back(link.btAddress, false, result);
                        }
                    }
                }
            }
            else if (
                code == kL2capConfigRsp && len >= 6 && link.aap.state == AapState::Configuring &&
                id == link.aap.pendingSigId)
            {
                const uint16_t result = ReadLe16(data, 4);
                if (result == 0x0000) {
                    link.aap.localConfigDone = true;
                    if (link.aap.remoteConfigDone) {
                        link.aap.state = AapState::Open;
                        link.aap.retryCount = 0;
                        deferred.logs.emplace_back(
                            LogLevel::Info, "Hidden L2CAP AAP channel OPEN (rcid=" +
                                                std::to_string(link.aap.remoteCid) + ", mtu=" +
                                                std::to_string(link.aap.remoteMtu) + ").");
                        deferred.aapStates.emplace_back(link.btAddress, true, 0u);
                    }
                }
                else {
                    deferred.logs.emplace_back(
                        LogLevel::Warn, "Hidden L2CAP AAP ConfigRsp failed (result=" +
                                            std::to_string(result) + ").");
                    link.aap = AapChannel{};
                    deferred.aapStates.emplace_back(link.btAddress, false, result);
                }
            }
            else if (code == kL2capDisconnRsp && link.aap.state == AapState::Disconnecting) {
                link.aap = AapChannel{};
            }
            else if (code == kL2capCmdReject && id == link.aap.pendingSigId) {
                const uint16_t reason = len >= 2 ? ReadLe16(data, 0) : 0u;
                deferred.logs.emplace_back(
                    LogLevel::Warn, "Hidden L2CAP AAP CmdReject (sigId=" + std::to_string(id) +
                                        ", reason=" + std::to_string(reason) + ").");
                const bool wasOpen = link.aap.state == AapState::Open;
                link.aap = AapChannel{};
                if (wasOpen) {
                    deferred.aapStates.emplace_back(link.btAddress, false, 1u);
                }
            }
        }
        // 2. Peer-initiated Configuration Request targeting our reserved local CID.
        else if (code == kL2capConfigReq && len >= 4 && IsReservedLocalCid(ReadLe16(data, 0))) {
            consume = true;
            ++_interceptedAclPackets;

            const uint16_t dcid = ReadLe16(data, 0);
            const uint16_t flags = ReadLe16(data, 2);
            if (dcid == link.aap.localCid && link.aap.remoteCid != 0) {
                // Parse optional remote MTU option (type 0x01, len 2).
                size_t optPos = 4;
                while (optPos + 2 <= data.size()) {
                    const uint8_t optType = static_cast<uint8_t>(data[optPos] & 0x7Fu);
                    const uint8_t optLen = data[optPos + 1];
                    if (optPos + 2 + optLen > data.size()) {
                        break;
                    }
                    if (optType == 0x01 && optLen == 2) {
                        link.aap.remoteMtu = ReadLe16(data, optPos + 2);
                    }
                    optPos += 2 + optLen;
                }

                const auto cfgRsp = BuildL2capConfigRsp(id, link.aap.remoteCid, flags & 0x0001u, 0);
                QueueL2capPduLocked(link.handle, kL2capSignalingCid, cfgRsp, deferred);

                if ((flags & 0x0001u) == 0) { // Final configuration fragment
                    link.aap.remoteConfigDone = true;
                    if (link.aap.localConfigDone && link.aap.state == AapState::Configuring) {
                        link.aap.state = AapState::Open;
                        link.aap.retryCount = 0;
                        deferred.logs.emplace_back(
                            LogLevel::Info, "Hidden L2CAP AAP channel OPEN (rcid=" +
                                                std::to_string(link.aap.remoteCid) + ", mtu=" +
                                                std::to_string(link.aap.remoteMtu) + ").");
                        deferred.aapStates.emplace_back(link.btAddress, true, 0u);
                    }
                }
            }
        }
        // 3. Peer-initiated Disconnection Request targeting our reserved local CID.
        else if (code == kL2capDisconnReq && len >= 4 && IsReservedLocalCid(ReadLe16(data, 0))) {
            consume = true;
            ++_interceptedAclPackets;

            const uint16_t dcid = ReadLe16(data, 0);
            const uint16_t scid = ReadLe16(data, 2);
            const auto rsp = BuildL2capDisconnRsp(id, dcid, scid);
            QueueL2capPduLocked(link.handle, kL2capSignalingCid, rsp, deferred);

            if (dcid == link.aap.localCid) {
                deferred.logs.emplace_back(
                    LogLevel::Info,
                    "Peer closed hidden L2CAP AAP channel (dcid=" + std::to_string(dcid) +
                        ", scid=" + std::to_string(scid) + ").");
                const bool wasOpen = link.aap.state == AapState::Open;
                link.aap = AapChannel{};
                if (wasOpen) {
                    deferred.aapStates.emplace_back(link.btAddress, false, 0u);
                }
            }
        }
        // 4. Optional HCI-level HFP protection: decline peer-initiated RFCOMM (PSM 0x0003)
        //    only on tracked AirPods links so other Bluetooth devices are never affected.
        else if (
            code == kL2capConnReq && len >= 4 && _config.declineHfp &&
            IsTrackedAirPodsAddressLocked(link.btAddress) && ReadLe16(data, 0) == kRfcommPsm)
        {
            consume = true;
            ++_interceptedAclPackets;
            ++_declinedHfpRequests;

            const uint16_t remoteScid = ReadLe16(data, 2);
            // Result 0x0004 = Connection refused - no resources available.
            const auto rsp = BuildL2capConnRsp(id, 0x0000, remoteScid, 0x0004, 0x0000);
            QueueL2capPduLocked(link.handle, kL2capSignalingCid, rsp, deferred);
            deferred.logs.emplace_back(
                LogLevel::Info, "Declined AirPods RFCOMM/HFP connection request to protect A2DP.");
        }
        // 5. Snoop AVDTP channel setup (both Windows-initiated and peer-initiated) so we know
        //    media/signaling CIDs and trigger deferred hidden AAP right once A2DP settles.
        else if (code == kL2capConnReq && len >= 4 && ReadLe16(data, 0) == kAvdtpPsm) {
            const uint16_t remoteScid = ReadLe16(data, 2);
            if (link.avdtp.signalRemoteCid == 0) {
                link.avdtp.signalRemoteCid = remoteScid;
            }
            else {
                link.avdtp.mediaRemoteCid = remoteScid;
            }
        }
        else if (code == kL2capConnRsp && len >= 8) {
            const uint16_t dcid = ReadLe16(data, 0);
            const uint16_t scid = ReadLe16(data, 2);
            const uint16_t result = ReadLe16(data, 4);
            if (result == 0x0000) {
                if (scid == link.avdtp.signalLocalCid && scid != 0) {
                    link.avdtp.signalRemoteCid = dcid;
                    link.avdtp.signalOpen = true;
                    if (link.encrypted && link.aap.state == AapState::WaitingForLink &&
                        IsTrackedAirPodsAddressLocked(link.btAddress))
                    {
                        BeginAapConnectLocked(link, deferred);
                    }
                }
                else if (scid == link.avdtp.mediaLocalCid && scid != 0) {
                    link.avdtp.mediaRemoteCid = dcid;
                }
            }
        }

        if (!consume) {
            keptSignaling.insert(keptSignaling.end(), cmdBytes.begin(), cmdBytes.end());
        }

        pos += 4 + len;
    }

    if (keptSignaling.empty()) {
        return;
    }

    // Re-wrap remaining Windows signaling commands in a valid HCI ACL L2CAP packet.
    std::vector<uint8_t> l2cap;
    l2cap.reserve(4 + keptSignaling.size());
    PushLe16(l2cap, static_cast<uint16_t>(keptSignaling.size()));
    PushLe16(l2cap, kL2capSignalingCid);
    l2cap.insert(l2cap.end(), keptSignaling.begin(), keptSignaling.end());
    AppendRawAclFragment(forwardToWindows, link.handle, 0x02, l2cap);
}

void HciProxyEngine::DeliverAapSduLocked(
    AclLink &link, std::span<const uint8_t> sdu, DeferredNotifications &deferred)
{
    if (sdu.empty()) {
        return;
    }
    deferred.aapPackets.emplace_back(link.btAddress, std::vector<uint8_t>{sdu.begin(), sdu.end()});
}

void HciProxyEngine::SnoopWindowsAclPacketLocked(
    AclLink &link, uint8_t pbFlag, std::span<const uint8_t> data, DeferredNotifications &deferred)
{
    if (pbFlag == 0x01 || data.size() < 4) {
        return;
    }
    const uint16_t l2capLen = ReadLe16(data, 0);
    const uint16_t l2capCid = ReadLe16(data, 2);
    const auto body = data.subspan(4);

    if (l2capCid == kL2capSignalingCid && body.size() >= 8) {
        size_t pos = 0;
        while (pos + 4 <= body.size() && pos < l2capLen) {
            const uint8_t code = body[pos];
            const uint16_t len = ReadLe16(body, pos + 2);
            if (pos + 4 + len > body.size()) {
                break;
            }
            if (code == kL2capConnReq && len >= 4) {
                const uint16_t psm = ReadLe16(body, pos + 4);
                const uint16_t scid = ReadLe16(body, pos + 6);
                if (psm == kAvdtpPsm) {
                    if (link.avdtp.signalLocalCid == 0) {
                        link.avdtp.signalLocalCid = scid;
                    }
                    else {
                        link.avdtp.mediaLocalCid = scid;
                    }
                }
            }
            else if (code == kL2capConnRsp && len >= 8) {
                const uint16_t dcid = ReadLe16(body, pos + 4);
                const uint16_t scid = ReadLe16(body, pos + 6);
                const uint16_t result = ReadLe16(body, pos + 8);
                if (result == 0x0000 && scid == link.avdtp.signalRemoteCid && scid != 0) {
                    link.avdtp.signalLocalCid = dcid;
                    link.avdtp.signalOpen = true;
                    if (link.encrypted && link.aap.state == AapState::WaitingForLink &&
                        IsTrackedAirPodsAddressLocked(link.btAddress))
                    {
                        BeginAapConnectLocked(link, deferred);
                    }
                }
            }
            pos += 4 + len;
        }
    }
    else if (
        l2capCid != 0 && l2capCid == link.avdtp.signalRemoteCid && body.size() >= 2 &&
        (body[0] & 0x0Fu) == 0)
    {
        const uint8_t signal = static_cast<uint8_t>(body[1] & 0x3Fu);
        if (signal == 0x07) { // AVDTP_START
            link.avdtp.streaming = true;
        }
        else if (signal == 0x08 || signal == 0x09) { // AVDTP_CLOSE / AVDTP_SUSPEND
            link.avdtp.streaming = false;
        }
    }
}

void HciProxyEngine::AppendRawAclFragment(
    std::vector<uint8_t> &out, uint16_t handle, uint8_t pbFlag, std::span<const uint8_t> payload)
{
    const uint16_t hdr =
        static_cast<uint16_t>((handle & 0x0FFFu) | ((static_cast<uint16_t>(pbFlag) & 0x03u) << 12));
    PushLe16(out, hdr);
    PushLe16(out, static_cast<uint16_t>(payload.size()));
    out.insert(out.end(), payload.begin(), payload.end());
}

uint64_t HciProxyEngine::ReadBdAddrLe(std::span<const uint8_t> bytes)
{
    uint64_t addr = 0;
    for (int i = 0; i < 6; ++i) {
        addr |= static_cast<uint64_t>(bytes[static_cast<size_t>(i)]) << (i * 8);
    }
    return addr;
}

} // namespace Service::Protocol
