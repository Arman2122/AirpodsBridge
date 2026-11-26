// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

#include "Service/Protocol/ServiceProtocol.h"

namespace Service::Protocol {

//
// Pure, deterministic Bluetooth HCI + L2CAP + AVDTP + RFCOMM man-in-the-middle engine.
//
// Sits between Windows' native Bluetooth stack (`BTHPORT` over `usbip-win2`) and the real
// USB Bluetooth controller (`WinUSB`):
//   - Tracks ACL/SCO connection handles, encryption state, buffer limits, and BD_ADDR.
//   - Injects & completely hides private L2CAP channels for Apple Accessory Protocol
//     (PSM 0x1001) using reserved local CIDs (0x7F40..0x7FFF) and signaling IDs (0xC0..0xFE).
//   - Never interleaves injected ACL packets inside a fragmented Windows L2CAP PDU.
//   - Maintains exact HCI ACL controller credit accounting and subtracts injected packet
//     completions from `Number_Of_Completed_Packets` (0x13) so Windows' credit pool never
//     drifts.
//   - Rewrites `HCI_QoS_Setup` (0x0807, Guaranteed + low latency & delay variation), clamps
//     `HCI_Sniff_Mode` (0x0803) sleep intervals, and caps `HCI_Write_Automatic_Flush_Timeout`
//     (0x0C28) to 40 ms on tracked AirPods links when low-latency QoS is enabled.
//   - Supports pull-mode ACL injection so a single Bulk OUT owner pops injected packets right
//     before writing to WinUSB, making Windows L2CAP fragment interleaving impossible.
//   - Snoops AVDTP (PSM 0x0019) and optionally declines AirPods RFCOMM HFP (PSM 0x0003)
//     requests when HFP protection is enabled, without affecting non-AirPods devices.
//
class HciProxyEngine
{
public:
    static constexpr uint16_t kAapPsm = 0x1001;
    static constexpr uint16_t kRfcommPsm = 0x0003;
    static constexpr uint16_t kAvdtpPsm = 0x0019;
    static constexpr uint16_t kFirstReservedCid = 0x7F40;
    static constexpr uint16_t kLastReservedCid = 0x7FFF;
    static constexpr uint8_t kFirstReservedSigId = 0xC0;
    static constexpr uint8_t kLastReservedSigId = 0xFE;
    static constexpr uint16_t kOurAapMtu = 2582;
    static constexpr uint32_t kDefaultPollLatencyUs = 3750;

    enum class LogLevel : uint8_t { Trace = 0, Info = 1, Warn = 2, Error = 3 };

    struct Callbacks {
        std::function<void(std::vector<uint8_t>)> sendAclToController;
        std::function<void()> onInjectedAclAvailable;
        std::function<void(std::vector<uint8_t>)> sendCommandToController;
        std::function<void(uint64_t btAddress, bool connected, uint32_t errorCode)>
            onAapConnectionChanged;
        std::function<void(uint64_t btAddress, std::vector<uint8_t> payload)> onAapPacketReceived;
        std::function<void(uint64_t btAddress, bool active)> onCallAudioChanged;
        std::function<void(std::string reason)> onControllerTrouble;
        std::function<void(LogLevel level, std::string message)> onLog;
    };

    struct Telemetry {
        uint16_t hciManufacturer{0};
        uint64_t localAddress{0};
        uint16_t aclMaxLength{1021};
        uint16_t aclTotalBuffers{0};
        uint32_t activeAclLinks{0};
        uint32_t activeAapChannels{0};
        bool callAudioActive{false};
        uint64_t injectedAclPackets{0};
        uint64_t interceptedAclPackets{0};
        uint64_t rewrittenQosCommands{0};
        uint64_t declinedHfpRequests{0};
    };

    explicit HciProxyEngine(Callbacks callbacks = {});

    void SetCallbacks(Callbacks callbacks);
    void SetConfig(const ProxyConfig &config);
    ProxyConfig GetConfig() const;
    // When true, defers opening hidden L2CAP AAP until Windows' AVDTP/A2DP channel settles
    // (or 1.2 s after link encryption) so PSM 0x1001 never collides with initial profile setup.
    void SetDeferAapUntilAvdtp(bool defer);
    // When true, injected ACL packets stay queued inside `HciProxyEngine` and signal
    // `callbacks.onInjectedAclAvailable` so the single Bulk OUT owner pulls them via
    // `PopNextInjectedAcl()` strictly at L2CAP PDU boundaries right before WinUSB writes.
    void SetPullModeForInjectedAcl(bool pullMode);

    // Pulls one ready injected ACL packet if `windowsPduRemaining == 0` and controller credits
    // permit. Must be called only by the Bulk OUT pipe owner thread.
    bool PopNextInjectedAcl(std::vector<uint8_t> &outPacket, uint16_t &outHandle);
    // Releases controller credit accounting if a pulled injected ACL write failed at WinUSB.
    void RevertInjectedAcl(uint16_t handle);

    // Resets controller/link state (e.g. radio detached or HCI_Reset).
    void Reset(std::string_view reason);

    // Periodic timer hook (deterministic monotonic milliseconds) that triggers deferred AAP
    // opens and retries stalled L2CAP handshakes.
    void Tick(uint64_t nowMs);

    // Control plane (called by Named Pipe clients / RadioEngine).
    void OpenAapChannel(uint64_t btAddress);
    void CloseAapChannel(uint64_t btAddress);
    bool SendAapPacket(uint64_t btAddress, std::span<const uint8_t> payload);

    // Data plane (called by USB/IP <-> WinUSB bridge threads).
    // Returns true if the command should be forwarded to the controller (may be modified in-place).
    bool ProcessWindowsCommand(std::vector<uint8_t> &command);

    // Inspects Windows outbound Bulk OUT ACL bytes, updates fragmentation/credit state, and
    // writes packets to send to the controller into `forwardOut`. May also flush queued
    // service ACL packets cleanly at L2CAP PDU boundaries via `callbacks.sendAclToController`.
    void ProcessWindowsAclOut(
        std::span<const uint8_t> bulkOut, std::vector<uint8_t> &forwardToController);

    // Streams controller Interrupt IN HCI event bytes, updating link/credit state and writing
    // filtered events to deliver to Windows into `forwardToWindows`.
    void ProcessControllerEvents(
        std::span<const uint8_t> interruptIn, std::vector<uint8_t> &forwardToWindows);

    // Streams controller Bulk IN HCI ACL bytes, intercepting AAP L2CAP signaling/data and
    // writing non-intercepted packets to deliver to Windows into `forwardToWindows`.
    void
    ProcessControllerAclIn(std::span<const uint8_t> bulkIn, std::vector<uint8_t> &forwardToWindows);

    Telemetry GetTelemetry() const;
    bool IsAapChannelOpen(uint64_t btAddress) const;

private:
    enum class AapState : uint8_t {
        Idle = 0,
        WaitingForLink = 1,
        Connecting = 2,
        Configuring = 3,
        Open = 4,
        Disconnecting = 5,
    };

    struct AapChannel {
        AapState state{AapState::Idle};
        uint16_t localCid{0};
        uint16_t remoteCid{0};
        uint16_t remoteMtu{672};
        uint8_t pendingSigId{0};
        bool localConfigDone{false};
        bool remoteConfigDone{false};
        uint8_t retryCount{0};
        uint64_t stateEnteredMs{0};
        std::vector<uint8_t> rxReassembly;
        uint16_t rxExpectedLen{0};
    };

    struct AvdtpTrack {
        uint16_t signalLocalCid{0};
        uint16_t signalRemoteCid{0};
        uint16_t mediaLocalCid{0};
        uint16_t mediaRemoteCid{0};
        bool signalOpen{false};
        bool streaming{false};
    };

    struct AclLink {
        uint16_t handle{0};
        uint64_t btAddress{0};
        bool encrypted{false};
        uint32_t ourOutstandingPackets{0};
        uint32_t windowsPduRemaining{0};
        // Inbound L2CAP continuation routing state for this ACL handle.
        bool rxIntercepting{false};
        uint16_t rxActiveLocalCid{0};
        uint16_t rxActiveExpectedLen{0};
        std::vector<uint8_t> rxSignalingBuf;
        AapChannel aap;
        AvdtpTrack avdtp;
    };

    struct QueuedAclPacket {
        uint16_t handle{0};
        std::vector<uint8_t> packet;
    };

    struct DeferredNotifications {
        std::vector<std::vector<uint8_t>> aclToSend;
        bool notifyInjectedAcl{false};
        std::vector<std::vector<uint8_t>> cmdToSend;
        std::vector<std::tuple<uint64_t, bool, uint32_t>> aapStates;
        std::vector<std::pair<uint64_t, std::vector<uint8_t>>> aapPackets;
        std::vector<std::pair<uint64_t, bool>> callAudioStates;
        std::vector<std::string> troubles;
        std::vector<std::pair<LogLevel, std::string>> logs;
    };

    void DispatchDeferred(DeferredNotifications &deferred);

    uint16_t AllocateLocalCidLocked();
    uint8_t AllocateSigIdLocked();
    AclLink *FindLinkByAddressLocked(uint64_t btAddress);
    const AclLink *FindLinkByAddressLocked(uint64_t btAddress) const;
    bool IsTrackedAirPodsAddressLocked(uint64_t btAddress) const;

    void BeginAapConnectLocked(AclLink &link, DeferredNotifications &deferred);
    void QueueL2capPduLocked(
        uint16_t handle, uint16_t remoteCid, std::span<const uint8_t> payload,
        DeferredNotifications &deferred);
    void FlushQueuedAclLocked(DeferredNotifications &deferred);
    bool CanInjectOnHandleLocked(const AclLink &link) const;

    // Event handlers.
    void HandleSingleEventLocked(
        std::span<const uint8_t> evt, std::vector<uint8_t> &forwardToWindows,
        DeferredNotifications &deferred);
    void HandleNumCompletedPacketsLocked(
        std::span<const uint8_t> evt, std::vector<uint8_t> &forwardToWindows,
        DeferredNotifications &deferred);

    // Inbound ACL & L2CAP signaling handlers.
    void HandleControllerAclPacketLocked(
        std::span<const uint8_t> pkt, std::vector<uint8_t> &forwardToWindows,
        DeferredNotifications &deferred);
    void HandleControllerSignalingPduLocked(
        AclLink &link, std::span<const uint8_t> sigPdu, std::vector<uint8_t> &forwardToWindows,
        DeferredNotifications &deferred);
    void DeliverAapSduLocked(
        AclLink &link, std::span<const uint8_t> sdu, DeferredNotifications &deferred);

    // Outbound Windows L2CAP snooping.
    void SnoopWindowsAclPacketLocked(
        AclLink &link, uint8_t pbFlag, std::span<const uint8_t> data,
        DeferredNotifications &deferred);

    static void AppendRawAclFragment(
        std::vector<uint8_t> &out, uint16_t handle, uint8_t pbFlag,
        std::span<const uint8_t> payload);
    static uint64_t ReadBdAddrLe(std::span<const uint8_t> bytes);

    mutable std::mutex _mutex;
    Callbacks _callbacks;
    ProxyConfig _config{};
    bool _deferAapUntilAvdtp{false};
    bool _pullModeInjectedAcl{false};
    uint64_t _lastTickMs{0};

    bool _aclFramed{false};
    uint16_t _aclMaxLength{1021};
    uint16_t _aclTotalBuffers{0};
    uint32_t _totalOurOutstanding{0};
    uint16_t _nextLocalCid{kFirstReservedCid};
    uint8_t _nextSigId{kFirstReservedSigId};
    uint16_t _hciManufacturer{0};
    uint64_t _localAddress{0};
    std::vector<uint8_t> _lastLocalVersion;
    uint16_t _ownPendingCommandOpcode{0};

    // Addresses the client wants AAP opened on (persists across disconnects/reconnects).
    std::unordered_map<uint64_t, bool> _wantedAapAddresses;
    std::unordered_map<uint16_t, AclLink> _aclLinks;
    std::unordered_map<uint16_t, uint64_t> _scoLinks;
    std::deque<QueuedAclPacket> _ourAclQueue;

    // Incremental stream buffers for USB transfers that split or coalesce HCI packets.
    std::vector<uint8_t> _eventStreamBuf;
    std::vector<uint8_t> _aclInStreamBuf;

    // Telemetry counters.
    uint64_t _injectedAclPackets{0};
    uint64_t _interceptedAclPackets{0};
    uint64_t _rewrittenQosCommands{0};
    uint64_t _declinedHfpRequests{0};
};

} // namespace Service::Protocol
