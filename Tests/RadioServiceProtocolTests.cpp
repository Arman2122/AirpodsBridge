// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <cmath>
#include <cstdint>
#include <vector>

#include <QtTest>

#include "Service/Protocol/HciProxyEngine.h"
#include "Service/Protocol/ServiceProtocol.h"
#include "Service/Protocol/Uac1MicEmulator.h"
#include "Service/Protocol/UsbIpProtocol.h"
#include "Service/Protocol/WatchdogPolicy.h"

using namespace Service::Protocol;

namespace {

uint16_t ReadLe16(const std::vector<uint8_t> &b, size_t off)
{
    return static_cast<uint16_t>(b[off]) |
           static_cast<uint16_t>(static_cast<uint16_t>(b[off + 1]) << 8);
}

uint32_t ReadLe32(const std::vector<uint8_t> &b, size_t off)
{
    return static_cast<uint32_t>(b[off]) | (static_cast<uint32_t>(b[off + 1]) << 8) |
           (static_cast<uint32_t>(b[off + 2]) << 16) | (static_cast<uint32_t>(b[off + 3]) << 24);
}

std::vector<uint8_t>
MakeAclPkt(uint16_t handle, uint8_t pbFlag, const std::vector<uint8_t> &payload)
{
    const uint16_t hdr =
        static_cast<uint16_t>((handle & 0x0FFFu) | ((static_cast<uint16_t>(pbFlag) & 0x03u) << 12));
    std::vector<uint8_t> pkt;
    pkt.push_back(static_cast<uint8_t>(hdr & 0xFFu));
    pkt.push_back(static_cast<uint8_t>((hdr >> 8) & 0xFFu));
    pkt.push_back(static_cast<uint8_t>(payload.size() & 0xFFu));
    pkt.push_back(static_cast<uint8_t>((payload.size() >> 8) & 0xFFu));
    pkt.insert(pkt.end(), payload.begin(), payload.end());
    return pkt;
}

std::vector<uint8_t> MakeL2capPkt(uint16_t handle, uint16_t cid, const std::vector<uint8_t> &body)
{
    std::vector<uint8_t> l2cap;
    l2cap.push_back(static_cast<uint8_t>(body.size() & 0xFFu));
    l2cap.push_back(static_cast<uint8_t>((body.size() >> 8) & 0xFFu));
    l2cap.push_back(static_cast<uint8_t>(cid & 0xFFu));
    l2cap.push_back(static_cast<uint8_t>((cid >> 8) & 0xFFu));
    l2cap.insert(l2cap.end(), body.begin(), body.end());
    return MakeAclPkt(handle, 0x02, l2cap);
}

} // namespace

class RadioServiceProtocolTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ServiceProtocolFramingAndStatusRoundTrip();
    void WatchdogCircuitBreakerAndHealthyReset();
    void UsbIpWireCodecAndIsoCompaction();
    void HciProxyHiddenAapHandshakeAndDataIsolation();
    void HciProxyRetriesTransientL2capRejection();
    void HciProxyDefersAapUntilAvdtpAndRetriesOnStall();
    void HciProxyNeverInterleavesWindowsFragmentedPdu();
    void HciProxyPullModeNeverInterleavesAndRevertsWriteErrors();
    void HciProxyRewritesQosAndDeclinesHfpOnlyForAirPods();
    void Uac1MicEmulatorDescriptorsAndAdaptiveAudio();
};

void RadioServiceProtocolTests::ServiceProtocolFramingAndStatusRoundTrip()
{
    StatusSnapshot snap{};
    snap.state = RadioServiceState::Running;
    snap.usbipDriverReady = true;
    snap.winUsbBound = true;
    snap.virtualAdapterAttached = true;
    snap.adapterVid = 0x8087;
    snap.adapterPid = 0x0036;
    snap.hciManufacturer = 0x0002;
    snap.activeAapChannels = 1;
    snap.activePollLatencyUs = 3750;
    snap.injectedPackets = 42;
    snap.interceptedPackets = 99;
    snap.adapterInstanceId = "USB\\VID_8087&PID_0036\\5&1234";
    snap.adapterDisplayName = "Intel(R) Wireless Bluetooth(R)";
    snap.detail = "Healthy";

    const auto payload = EncodeStatusSnapshot(snap);
    const auto frameBytes = EncodeFrame(MessageType::StatusSnapshot, payload);

    // Partial frame returns NeedMoreData.
    Frame decoded{};
    size_t consumed = 0;
    QCOMPARE(
        TryDecodeFrame(std::span<const uint8_t>{frameBytes.data(), 7}, decoded, consumed),
        DecodeStatus::NeedMoreData);

    // Full frame decodes cleanly.
    QCOMPARE(TryDecodeFrame(frameBytes, decoded, consumed), DecodeStatus::Ok);
    QCOMPARE(consumed, frameBytes.size());
    QCOMPARE(decoded.type, MessageType::StatusSnapshot);

    const auto decodedSnap = DecodeStatusSnapshot(decoded.payload);
    QVERIFY(decodedSnap.has_value());
    QVERIFY(decodedSnap.value() == snap);

    // Corrupt magic fails explicitly.
    auto bad = frameBytes;
    bad[0] = 'X';
    QCOMPARE(TryDecodeFrame(bad, decoded, consumed), DecodeStatus::InvalidFrame);

    // ProxyConfig & AapPacketMsg round-trip.
    const ProxyConfig cfg{
        .lowLatencyQos = true,
        .pollLatencyUs = 3750,
        .declineHfp = true,
        .virtualMicEnabled = true,
    };
    QCOMPARE(DecodeProxyConfig(EncodeProxyConfig(cfg)), std::optional{cfg});

    const AapPacketMsg pkt{.bluetoothAddress = 0xF433B7263664ull, .data = {0x04, 0x00, 0x04, 0x00}};
    QCOMPARE(DecodeAapPacket(EncodeAapPacket(pkt)), std::optional{pkt});
}

void RadioServiceProtocolTests::WatchdogCircuitBreakerAndHealthyReset()
{
    WatchdogPolicy wd;
    QVERIFY(!wd.IsGuardTripped());

    // 1st and 2nd failures retry with increasing backoff.
    const auto f1 = wd.RecordTakeoverFailure(10'000);
    QCOMPARE(f1.action, WatchdogPolicy::FailureAction::RetryWithBackoff);
    QCOMPARE(f1.failuresInWindow, 1u);
    QVERIFY(!wd.IsGuardTripped());

    const auto f2 = wd.RecordTakeoverFailure(20'000);
    QCOMPARE(f2.action, WatchdogPolicy::FailureAction::RetryWithBackoff);
    QCOMPARE(f2.failuresInWindow, 2u);
    QVERIFY(f2.backoffMs > f1.backoffMs);

    // A healthy session lasting >= 60s clears prior failures.
    wd.MarkSessionStarted(30'000);
    wd.MarkSessionHealthyIfElapsed(95'000);
    QCOMPARE(wd.RecentFailureCount(95'000), 0u);

    // 3 failures within 10 minutes trip the guard and order native driver restore.
    QCOMPARE(
        wd.RecordTakeoverFailure(100'000).action, WatchdogPolicy::FailureAction::RetryWithBackoff);
    QCOMPARE(
        wd.RecordTakeoverFailure(110'000).action, WatchdogPolicy::FailureAction::RetryWithBackoff);
    const auto f3 = wd.RecordTakeoverFailure(120'000);
    QCOMPARE(f3.action, WatchdogPolicy::FailureAction::TripGuardAndRestoreNative);
    QVERIFY(wd.IsGuardTripped());

    wd.ClearGuard();
    QVERIFY(!wd.IsGuardTripped());

    // Adapter reset budget caps at 3 per 24h.
    QVERIFY(wd.AllowAdapterReset(1'000));
    QVERIFY(wd.AllowAdapterReset(2'000));
    QVERIFY(wd.AllowAdapterReset(3'000));
    QVERIFY(!wd.AllowAdapterReset(4'000));
}

void RadioServiceProtocolTests::UsbIpWireCodecAndIsoCompaction()
{
    // Build a Bluetooth config descriptor where HCI endpoints sit on interface 2 (e.g. composite).
    const std::vector<uint8_t> cfg = {
        9,
        0x02,
        41,
        0,
        2,
        1,
        0,
        0x80,
        50,
        // Interface 0 (HID keyboard, not HCI)
        9,
        0x04,
        0,
        0,
        0,
        0x03,
        0x01,
        0x01,
        0,
        // Interface 2 Alt 0: Interrupt IN 0x81, Bulk IN 0x82, Bulk OUT 0x02
        9,
        0x04,
        2,
        0,
        3,
        0xE0,
        0x01,
        0x01,
        0,
        7,
        0x05,
        0x81,
        0x03,
        16,
        0,
        1,
        7,
        0x05,
        0x82,
        0x02,
        64,
        0,
        0,
        7,
        0x05,
        0x02,
        0x02,
        64,
        0,
        0,
    };
    const auto pipes = UsbIp::FindBluetoothHciPipes(cfg);
    QVERIFY(pipes.Valid());
    QCOMPARE(pipes.interfaceNumber, uint8_t{2});
    QCOMPARE(pipes.eventPipe, uint8_t{0x81});
    QCOMPARE(pipes.aclInPipe, uint8_t{0x82});
    QCOMPARE(pipes.aclOutPipe, uint8_t{0x02});

    // Verify front-packed IN isochronous compaction:
    // Packet 0 is empty, Packet 1 has 4 bytes {0xAA,0xBB,0xCC,0xDD}, Packet 2 is empty.
    // usbip-win2 requires non-empty payload to be packed into descriptor 0's actual_length so
    // bytes at offset 0 are never duplicated.
    std::vector<uint8_t> cmdDescs(3 * UsbIp::kIsoDescriptorBytes, 0);
    for (uint32_t i = 0; i < 3; ++i) {
        UsbIp::WriteBe32(cmdDescs, i * 16, i * 8);
        UsbIp::WriteBe32(cmdDescs, i * 16 + 4, 8);
    }

    UsbIp::IsoTransferResult iso{};
    iso.data = {0, 0, 0, 0, 0, 0, 0, 0, 0xAA, 0xBB, 0xCC, 0xDD, 0, 0, 0, 0};
    iso.packets = {
        {.offset = 0, .actualLength = 0, .status = 0},
        {.offset = 8, .actualLength = 4, .status = 0},
        {.offset = 16, .actualLength = 0, .status = 0},
    };

    const auto ret = UsbIp::EncodeRetIso(7, 0, 100, cmdDescs, 3, true, &iso);
    QCOMPARE(UsbIp::ReadBe32(ret, 0), UsbIp::kRetSubmit);
    QCOMPARE(UsbIp::ReadBe32(ret, 4), 7u);
    QCOMPARE(UsbIp::ReadBe32(ret, 24), 4u); // total actual = 4 bytes
    QCOMPARE(ret[48], uint8_t{0xAA});
    QCOMPARE(ret[51], uint8_t{0xDD});

    // Descriptor 0 holds actual=4, descriptors 1 and 2 hold actual=0.
    const size_t descBase = UsbIp::kUrbHeaderBytes + 4;
    QCOMPARE(UsbIp::ReadBe32(ret, descBase + 8), 4u);
    QCOMPARE(UsbIp::ReadBe32(ret, descBase + 16 + 8), 0u);
    QCOMPARE(UsbIp::ReadBe32(ret, descBase + 32 + 8), 0u);
}

void RadioServiceProtocolTests::HciProxyHiddenAapHandshakeAndDataIsolation()
{
    constexpr uint64_t kAirPodsAddr = 0x112233445566ull;
    constexpr uint16_t kHandle = 0x000B;

    std::vector<std::vector<uint8_t>> injectedAcls;
    bool aapConnected = false;
    std::vector<std::vector<uint8_t>> receivedAapPackets;

    HciProxyEngine::Callbacks cb{};
    cb.sendAclToController = [&](std::vector<uint8_t> pkt) {
        injectedAcls.push_back(std::move(pkt));
    };
    cb.onAapConnectionChanged = [&](uint64_t addr, bool connected, uint32_t) {
        if (addr == kAirPodsAddr) {
            aapConnected = connected;
        }
    };
    cb.onAapPacketReceived = [&](uint64_t addr, std::vector<uint8_t> payload) {
        if (addr == kAirPodsAddr) {
            receivedAapPackets.push_back(std::move(payload));
        }
    };

    HciProxyEngine engine{cb};
    std::vector<uint8_t> winEvents;

    // 1. HCI_Reset complete -> enables ACL framing.
    const std::vector<uint8_t> evtReset = {0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00};
    engine.ProcessControllerEvents(evtReset, winEvents);
    QCOMPARE(winEvents, evtReset);

    // 2. Read_Buffer_Size complete -> maxLen=256, numBufs=6.
    const std::vector<uint8_t> evtBuf = {0x0E, 0x0B, 0x01, 0x05, 0x10, 0x00, 0x00,
                                         0x01, 0x40, 0x06, 0x00, 0x04, 0x00};
    engine.ProcessControllerEvents(evtBuf, winEvents);
    QCOMPARE(engine.GetTelemetry().aclMaxLength, uint16_t{256});
    QCOMPARE(engine.GetTelemetry().aclTotalBuffers, uint16_t{6});

    // 3. Connection_Complete (ACL, unencrypted) -> should NOT open AAP until encrypted.
    engine.OpenAapChannel(kAirPodsAddr);
    const std::vector<uint8_t> evtConn = {0x03, 0x0B, 0x00, 0x0B, 0x00, 0x66, 0x55,
                                          0x44, 0x33, 0x22, 0x11, 0x01, 0x00};
    engine.ProcessControllerEvents(evtConn, winEvents);
    QVERIFY(injectedAcls.empty());

    // 4. Encryption_Change (encrypted=1) -> injects hidden L2CAP ConnReq for PSM 0x1001.
    const std::vector<uint8_t> evtEnc = {0x08, 0x04, 0x00, 0x0B, 0x00, 0x01};
    engine.ProcessControllerEvents(evtEnc, winEvents);
    QCOMPARE(injectedAcls.size(), size_t{1});

    // Inspect injected L2CAP Connection Request.
    const auto &connReqAcl = injectedAcls.back();
    QCOMPARE(ReadLe16(connReqAcl, 6), uint16_t{0x0001}); // L2CAP signaling CID
    QCOMPARE(connReqAcl[8], uint8_t{0x02});              // ConnReq
    const uint8_t connSigId = connReqAcl[9];
    QVERIFY(connSigId >= HciProxyEngine::kFirstReservedSigId);
    QCOMPARE(ReadLe16(connReqAcl, 12), HciProxyEngine::kAapPsm);
    const uint16_t localCid = ReadLe16(connReqAcl, 14);
    QVERIFY(localCid >= HciProxyEngine::kFirstReservedCid);

    // 5. Controller returns Number_Of_Completed_Packets(handle=0x000B, count=1).
    // Since that 1 packet was ours, Windows must receive ZERO bytes (event suppressed).
    const std::vector<uint8_t> evtNocpOurs = {0x13, 0x05, 0x01, 0x0B, 0x00, 0x01, 0x00};
    engine.ProcessControllerEvents(evtNocpOurs, winEvents);
    QVERIFY(winEvents.empty());

    // 6. Peer replies with L2CAP ConnRsp (DCID=0x0044, SCID=localCid, Result=0).
    constexpr uint16_t kRemoteCid = 0x0044;
    const std::vector<uint8_t> connRspSig = {
        0x03,
        connSigId,
        0x08,
        0x00,
        0x44,
        0x00,
        static_cast<uint8_t>(localCid & 0xFFu),
        static_cast<uint8_t>((localCid >> 8) & 0xFFu),
        0x00,
        0x00,
        0x00,
        0x00,
    };
    std::vector<uint8_t> winAcl;
    engine.ProcessControllerAclIn(MakeL2capPkt(kHandle, 0x0001, connRspSig), winAcl);
    QVERIFY(winAcl.empty());                  // Completely hidden from Windows!
    QCOMPARE(injectedAcls.size(), size_t{2}); // Our ConfigReq sent automatically!

    const uint8_t cfgSigId = injectedAcls.back()[9];
    // Complete our ConfigReq + peer ConfigReq -> channel opens!
    engine.ProcessControllerEvents(evtNocpOurs, winEvents);
    const std::vector<uint8_t> cfgRspSig = {
        0x05,
        cfgSigId,
        0x06,
        0x00,
        static_cast<uint8_t>(localCid & 0xFFu),
        static_cast<uint8_t>((localCid >> 8) & 0xFFu),
        0x00,
        0x00,
        0x00,
        0x00,
    };
    engine.ProcessControllerAclIn(MakeL2capPkt(kHandle, 0x0001, cfgRspSig), winAcl);
    QVERIFY(winAcl.empty());
    QVERIFY(!aapConnected); // Still waiting for peer's ConfigReq.

    const std::vector<uint8_t> peerCfgReq = {
        0x04,
        0x09,
        0x04,
        0x00,
        static_cast<uint8_t>(localCid & 0xFFu),
        static_cast<uint8_t>((localCid >> 8) & 0xFFu),
        0x00,
        0x00,
    };
    engine.ProcessControllerAclIn(MakeL2capPkt(kHandle, 0x0001, peerCfgReq), winAcl);
    QVERIFY(winAcl.empty());
    QVERIFY(aapConnected);
    QVERIFY(engine.IsAapChannelOpen(kAirPodsAddr));

    // 7. Feed a fragmented inbound AAP packet across PB=0x02 and PB=0x01.
    const std::vector<uint8_t> aapPayload = {0x04, 0x00, 0x04, 0x00, 0x04,
                                             0x00, 0x01, 0x04, 0x01, 88};
    std::vector<uint8_t> frag1 = {
        static_cast<uint8_t>(aapPayload.size()),
        0x00,
        static_cast<uint8_t>(localCid & 0xFFu),
        static_cast<uint8_t>((localCid >> 8) & 0xFFu),
        aapPayload[0],
        aapPayload[1],
        aapPayload[2],
        aapPayload[3],
    };
    std::vector<uint8_t> frag2(aapPayload.begin() + 4, aapPayload.end());

    engine.ProcessControllerAclIn(MakeAclPkt(kHandle, 0x02, frag1), winAcl);
    QVERIFY(winAcl.empty());
    QVERIFY(receivedAapPackets.empty());

    engine.ProcessControllerAclIn(MakeAclPkt(kHandle, 0x01, frag2), winAcl);
    QVERIFY(winAcl.empty());
    QCOMPARE(receivedAapPackets.size(), size_t{1});
    QCOMPARE(receivedAapPackets[0], aapPayload);

    // 8. A Windows non-AAP ACL packet on CID 0x0041 passes straight through untouched.
    const auto winAudioPkt = MakeL2capPkt(kHandle, 0x0041, {0xDE, 0xAD, 0xBE, 0xEF});
    engine.ProcessControllerAclIn(winAudioPkt, winAcl);
    QCOMPARE(winAcl, winAudioPkt);

    // 9. Mixed Number_Of_Completed_Packets (1 ours + 3 Windows = 4 total) -> Windows sees 3!
    const std::vector<uint8_t> evtNocpMixed = {0x13, 0x05, 0x01, 0x0B, 0x00, 0x04, 0x00};
    engine.ProcessControllerEvents(evtNocpMixed, winEvents);
    QCOMPARE(winEvents.size(), size_t{7});
    QCOMPARE(ReadLe16(winEvents, 5), uint16_t{3});
}

void RadioServiceProtocolTests::HciProxyRetriesTransientL2capRejection()
{
    constexpr uint64_t kAddr = 0x112233445566ull;
    constexpr uint16_t kHandle = 0x000D;

    std::vector<std::vector<uint8_t>> injectedAcls;
    HciProxyEngine::Callbacks cb{};
    cb.sendAclToController = [&](std::vector<uint8_t> pkt) {
        injectedAcls.push_back(std::move(pkt));
    };

    HciProxyEngine engine{cb};
    std::vector<uint8_t> winEvents;
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00}, winEvents);
    engine.OpenAapChannel(kAddr);
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{
            0x03, 0x0B, 0x00, 0x0D, 0x00, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x01, 0x01},
        winEvents);
    QCOMPARE(injectedAcls.size(), size_t{1});

    const uint8_t firstSigId = injectedAcls.back()[9];
    const uint16_t firstCid = ReadLe16(injectedAcls.back(), 14);

    // Controller acknowledges completion of our first ConnReq so an ACL credit slot is free.
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{0x13, 0x05, 0x01, 0x0D, 0x00, 0x01, 0x00}, winEvents);
    QVERIFY(winEvents.empty());

    // Peer temporarily rejects ConnReq with Result = 0x0003 (Security Block).
    // Engine should swallow the rejection and automatically inject a retry ConnReq.
    const std::vector<uint8_t> rejectSig = {
        0x03,
        firstSigId,
        0x08,
        0x00,
        0x00,
        0x00,
        static_cast<uint8_t>(firstCid & 0xFFu),
        static_cast<uint8_t>((firstCid >> 8) & 0xFFu),
        0x03,
        0x00,
        0x00,
        0x00,
    };
    std::vector<uint8_t> winAcl;
    engine.ProcessControllerAclIn(MakeL2capPkt(kHandle, 0x0001, rejectSig), winAcl);
    QVERIFY(winAcl.empty());
    QCOMPARE(injectedAcls.size(), size_t{2});
    QCOMPARE(injectedAcls.back()[8], uint8_t{0x02});
}

void RadioServiceProtocolTests::HciProxyDefersAapUntilAvdtpAndRetriesOnStall()
{
    constexpr uint64_t kAddr = 0x112233445566ull;
    constexpr uint16_t kHandle = 0x000E;

    std::vector<std::vector<uint8_t>> injectedAcls;
    HciProxyEngine::Callbacks cb{};
    cb.sendAclToController = [&](std::vector<uint8_t> pkt) {
        injectedAcls.push_back(std::move(pkt));
    };

    HciProxyEngine engine{cb};
    engine.SetDeferAapUntilAvdtp(true);
    engine.Tick(1000);

    std::vector<uint8_t> winEvents;
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00}, winEvents);
    engine.OpenAapChannel(kAddr);
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{
            0x03, 0x0B, 0x00, 0x0E, 0x00, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x01, 0x00},
        winEvents);
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{0x08, 0x04, 0x00, 0x0E, 0x00, 0x01}, winEvents);

    // Must NOT inject ConnReq yet while Windows is still establishing AVDTP.
    QVERIFY(injectedAcls.empty());

    // Windows opens AVDTP signaling channel (PSM 0x0019, SCID 0x0041) -> peer replies OK.
    std::vector<uint8_t> fwdOut;
    const std::vector<uint8_t> avdtpReq = {0x02, 0x01, 0x04, 0x00, 0x19, 0x00, 0x41, 0x00};
    engine.ProcessWindowsAclOut(MakeL2capPkt(kHandle, 0x0001, avdtpReq), fwdOut);
    QVERIFY(injectedAcls.empty());

    std::vector<uint8_t> winAcl;
    const std::vector<uint8_t> avdtpRsp = {0x03, 0x01, 0x08, 0x00, 0x51, 0x00,
                                           0x41, 0x00, 0x00, 0x00, 0x00, 0x00};
    engine.ProcessControllerAclIn(MakeL2capPkt(kHandle, 0x0001, avdtpRsp), winAcl);
    QCOMPARE(winAcl.size(), MakeL2capPkt(kHandle, 0x0001, avdtpRsp).size());

    // Now that AVDTP is up, hidden AAP ConnReq is injected immediately!
    QCOMPARE(injectedAcls.size(), size_t{1});
    QCOMPARE(injectedAcls.back()[8], uint8_t{0x02});

    // Return ACL credit, then simulate a 3-second stall with no ConnRsp -> Tick() retries!
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{0x13, 0x05, 0x01, 0x0E, 0x00, 0x01, 0x00}, winEvents);
    engine.Tick(2500);
    QCOMPARE(injectedAcls.size(), size_t{1});
    engine.Tick(4100);
    QCOMPARE(injectedAcls.size(), size_t{2});
    QCOMPARE(injectedAcls.back()[8], uint8_t{0x02});
}

void RadioServiceProtocolTests::HciProxyNeverInterleavesWindowsFragmentedPdu()
{
    constexpr uint64_t kAddr = 0xAABBCCDDEEFFull;
    constexpr uint16_t kHandle = 0x000C;

    std::vector<std::vector<uint8_t>> injected;
    HciProxyEngine::Callbacks cb{};
    cb.sendAclToController = [&](std::vector<uint8_t> p) { injected.push_back(std::move(p)); };
    HciProxyEngine engine{cb};

    std::vector<uint8_t> dummy;
    engine.ProcessControllerEvents(std::vector<uint8_t>{0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00}, dummy);
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{
            0x03, 0x0B, 0x00, 0x0C, 0x00, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x01, 0x01},
        dummy);

    // Windows starts a 20-byte L2CAP PDU on CID 0x0050, sending only 8 bytes in the first fragment.
    std::vector<uint8_t> firstFragPayload = {20, 0, 0x50, 0x00, 1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> fwdOut;
    engine.ProcessWindowsAclOut(MakeAclPkt(kHandle, 0x02, firstFragPayload), fwdOut);
    QVERIFY(!fwdOut.empty());

    // Now request AAP open -> ConnReq MUST wait until Windows finishes its fragmented L2CAP PDU!
    engine.OpenAapChannel(kAddr);
    QVERIFY(injected.empty());

    // Windows sends the remaining 12 bytes in PB=0x01 -> now our ConnReq flushes immediately!
    std::vector<uint8_t> contFrag(12, 0x55);
    engine.ProcessWindowsAclOut(MakeAclPkt(kHandle, 0x01, contFrag), fwdOut);
    QCOMPARE(injected.size(), size_t{1});
}

void RadioServiceProtocolTests::HciProxyPullModeNeverInterleavesAndRevertsWriteErrors()
{
    constexpr uint64_t kAddr = 0xAABBCCDDEEFFull;
    constexpr uint16_t kHandle = 0x000F;

    int wakeups = 0;
    bool pushCallbackCalled = false;
    HciProxyEngine::Callbacks cb{};
    cb.sendAclToController = [&](std::vector<uint8_t>) { pushCallbackCalled = true; };
    cb.onInjectedAclAvailable = [&]() { ++wakeups; };

    HciProxyEngine engine{cb};
    engine.SetPullModeForInjectedAcl(true);

    std::vector<uint8_t> dummy;
    engine.ProcessControllerEvents(std::vector<uint8_t>{0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00}, dummy);
    // Controller reports 2 ACL buffer slots.
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{
            0x0E, 0x0A, 0x01, 0x05, 0x10, 0x00, 0x00, 0x01, 0x40, 0x02, 0x00, 0x00},
        dummy);
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{
            0x03, 0x0B, 0x00, 0x0F, 0x00, 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA, 0x01, 0x01},
        dummy);

    // Windows starts a 20-byte fragmented A2DP L2CAP PDU (first 8 bytes only).
    std::vector<uint8_t> firstFragPayload = {20, 0, 0x50, 0x00, 1, 2, 3, 4, 5, 6, 7, 8};
    std::vector<uint8_t> fwdOut;
    engine.ProcessWindowsAclOut(MakeAclPkt(kHandle, 0x02, firstFragPayload), fwdOut);

    // Request AAP open while Windows PDU is mid-flight -> MUST NOT push OR pop yet!
    engine.OpenAapChannel(kAddr);
    QVERIFY(!pushCallbackCalled);
    std::vector<uint8_t> popped;
    uint16_t poppedHandle = 0;
    QVERIFY(!engine.PopNextInjectedAcl(popped, poppedHandle));

    // Windows finishes the remaining 12 bytes -> wakes Bulk OUT owner and pops cleanly!
    std::vector<uint8_t> contFrag(12, 0x42);
    engine.ProcessWindowsAclOut(MakeAclPkt(kHandle, 0x01, contFrag), fwdOut);
    QVERIFY(wakeups > 0);
    QVERIFY(engine.PopNextInjectedAcl(popped, poppedHandle));
    QCOMPARE(poppedHandle, kHandle);
    QCOMPARE(popped[8], uint8_t{0x02}); // L2CAP ConnReq

    // Simulate a WinUSB write error on that popped packet -> RevertInjectedAcl releases credit
    // so subsequent injected packets are never starved.
    engine.RevertInjectedAcl(poppedHandle);
    engine.CloseAapChannel(kAddr);
    engine.OpenAapChannel(kAddr);
    QVERIFY(engine.PopNextInjectedAcl(popped, poppedHandle));
    QCOMPARE(poppedHandle, kHandle);
}

void RadioServiceProtocolTests::HciProxyRewritesQosAndDeclinesHfpOnlyForAirPods()
{
    constexpr uint64_t kAirPodsAddr = 0x102030405060ull;
    constexpr uint64_t kMouseAddr = 0x605040302010ull;
    constexpr uint16_t kAirPodsHandle = 0x000A;
    constexpr uint16_t kMouseHandle = 0x000D;

    std::vector<std::vector<uint8_t>> injected;
    HciProxyEngine::Callbacks cb{};
    cb.sendAclToController = [&](std::vector<uint8_t> p) { injected.push_back(std::move(p)); };
    HciProxyEngine engine{cb};
    engine.SetConfig(ProxyConfig{
        .lowLatencyQos = true,
        .pollLatencyUs = 3750,
        .declineHfp = true,
        .virtualMicEnabled = false,
    });
    engine.OpenAapChannel(kAirPodsAddr);

    std::vector<uint8_t> out;
    engine.ProcessControllerEvents(std::vector<uint8_t>{0x0E, 0x04, 0x01, 0x03, 0x0C, 0x00}, out);
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{
            0x03, 0x0B, 0x00, 0x0A, 0x00, 0x60, 0x50, 0x40, 0x30, 0x20, 0x10, 0x01, 0x00},
        out);
    engine.ProcessControllerEvents(
        std::vector<uint8_t>{
            0x03, 0x0B, 0x00, 0x0D, 0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x01, 0x00},
        out);

    // 1. HCI_QoS_Setup (0x0807): upgrades AirPods to Guaranteed (0x02) + 3750 us latency & jitter.
    auto makeQos = [](uint16_t handle) {
        std::vector<uint8_t> cmd(23, 0);
        cmd[0] = 0x07;
        cmd[1] = 0x08;
        cmd[2] = 20;
        cmd[3] = static_cast<uint8_t>(handle & 0xFFu);
        cmd[4] = static_cast<uint8_t>((handle >> 8) & 0xFFu);
        cmd[6] = 0x01;  // Best Effort
        cmd[15] = 0xA8; // 25000 = 0x000061A8
        cmd[16] = 0x61;
        cmd[19] = 0xA8;
        cmd[20] = 0x61;
        return cmd;
    };

    auto qosAirPods = makeQos(kAirPodsHandle);
    QVERIFY(engine.ProcessWindowsCommand(qosAirPods));
    QCOMPARE(qosAirPods[6], uint8_t{0x02}); // Guaranteed real-time QoS
    QCOMPARE(ReadLe32(qosAirPods, 15), 3750u);
    QCOMPARE(ReadLe32(qosAirPods, 19), 3750u);

    auto qosMouse = makeQos(kMouseHandle);
    QVERIFY(engine.ProcessWindowsCommand(qosMouse));
    QCOMPARE(qosMouse[6], uint8_t{0x01});
    QCOMPARE(ReadLe32(qosMouse, 15), 25000u); // Mouse untouched!
    QCOMPARE(ReadLe32(qosMouse, 19), 25000u);

    // 2. HCI_Sniff_Mode (0x0803): clamps 500 ms Windows sleep to 10 ms idle / 5 ms streaming.
    auto makeSniff = [](uint16_t handle, uint16_t maxSlots, uint16_t minSlots) {
        std::vector<uint8_t> cmd(13, 0);
        cmd[0] = 0x03;
        cmd[1] = 0x08;
        cmd[2] = 10;
        cmd[3] = static_cast<uint8_t>(handle & 0xFFu);
        cmd[4] = static_cast<uint8_t>((handle >> 8) & 0xFFu);
        cmd[5] = static_cast<uint8_t>(maxSlots & 0xFFu);
        cmd[6] = static_cast<uint8_t>((maxSlots >> 8) & 0xFFu);
        cmd[7] = static_cast<uint8_t>(minSlots & 0xFFu);
        cmd[8] = static_cast<uint8_t>((minSlots >> 8) & 0xFFu);
        cmd[9] = 1;  // attempt
        cmd[11] = 1; // timeout
        return cmd;
    };

    auto sniffAirPodsIdle = makeSniff(kAirPodsHandle, 0x0320, 0x0190); // 500 ms / 250 ms
    QVERIFY(engine.ProcessWindowsCommand(sniffAirPodsIdle));
    QCOMPARE(ReadLe16(sniffAirPodsIdle, 5), uint16_t{0x0010}); // Clamped to 10.0 ms idle
    QCOMPARE(ReadLe16(sniffAirPodsIdle, 7), uint16_t{0x0010});
    QCOMPARE(ReadLe16(sniffAirPodsIdle, 9), uint16_t{4});
    QCOMPARE(ReadLe16(sniffAirPodsIdle, 11), uint16_t{4});

    // Bring up AVDTP signaling and send AVDTP_START -> Sniff clamps even tighter to 5.0 ms!
    std::vector<uint8_t> fwdOut;
    engine.ProcessWindowsAclOut(
        MakeL2capPkt(kAirPodsHandle, 0x0001, {0x02, 0x01, 0x04, 0x00, 0x19, 0x00, 0x41, 0x00}),
        fwdOut);
    std::vector<uint8_t> winAclTmp;
    engine.ProcessControllerAclIn(
        MakeL2capPkt(
            kAirPodsHandle, 0x0001,
            {0x03, 0x01, 0x08, 0x00, 0x51, 0x00, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00}),
        winAclTmp);
    engine.ProcessWindowsAclOut(MakeL2capPkt(kAirPodsHandle, 0x0051, {0x10, 0x07}), fwdOut);

    auto sniffAirPodsStream = makeSniff(kAirPodsHandle, 0x0100, 0x0080);
    QVERIFY(engine.ProcessWindowsCommand(sniffAirPodsStream));
    QCOMPARE(ReadLe16(sniffAirPodsStream, 5), uint16_t{0x0008}); // Clamped to 5.0 ms streaming!

    auto sniffMouse = makeSniff(kMouseHandle, 0x0320, 0x0190);
    QVERIFY(engine.ProcessWindowsCommand(sniffMouse));
    QCOMPARE(ReadLe16(sniffMouse, 5), uint16_t{0x0320}); // Mouse untouched!

    // 3. HCI_Write_Automatic_Flush_Timeout (0x0C28): caps infinite (0) and > 64 down to 64 (40 ms).
    auto makeFlush = [](uint16_t handle, uint16_t flushSlots) {
        std::vector<uint8_t> cmd(7, 0);
        cmd[0] = 0x28;
        cmd[1] = 0x0C;
        cmd[2] = 4;
        cmd[3] = static_cast<uint8_t>(handle & 0xFFu);
        cmd[4] = static_cast<uint8_t>((handle >> 8) & 0xFFu);
        cmd[5] = static_cast<uint8_t>(flushSlots & 0xFFu);
        cmd[6] = static_cast<uint8_t>((flushSlots >> 8) & 0xFFu);
        return cmd;
    };

    auto flushInf = makeFlush(kAirPodsHandle, 0);
    QVERIFY(engine.ProcessWindowsCommand(flushInf));
    QCOMPARE(ReadLe16(flushInf, 5), uint16_t{64});

    auto flushHuge = makeFlush(kAirPodsHandle, 200);
    QVERIFY(engine.ProcessWindowsCommand(flushHuge));
    QCOMPARE(ReadLe16(flushHuge, 5), uint16_t{64});

    auto flushTight = makeFlush(kAirPodsHandle, 32);
    QVERIFY(engine.ProcessWindowsCommand(flushTight));
    QCOMPARE(ReadLe16(flushTight, 5), uint16_t{32}); // Already tighter -> preserved!

    auto flushMouse = makeFlush(kMouseHandle, 0);
    QVERIFY(engine.ProcessWindowsCommand(flushMouse));
    QCOMPARE(ReadLe16(flushMouse, 5), uint16_t{0}); // Mouse untouched!

    // Peer-initiated RFCOMM ConnReq (PSM 0x0003) is declined for AirPods, forwarded for Mouse.
    const std::vector<uint8_t> rfcommConnReq = {0x02, 0x05, 0x04, 0x00, 0x03, 0x00, 0x41, 0x00};
    std::vector<uint8_t> winAcl;
    engine.ProcessControllerAclIn(MakeL2capPkt(kAirPodsHandle, 0x0001, rfcommConnReq), winAcl);
    QVERIFY(winAcl.empty());
    QCOMPARE(engine.GetTelemetry().declinedHfpRequests, uint64_t{1});
    QCOMPARE(injected.size(), size_t{1});
    QCOMPARE(injected[0][8], uint8_t{0x03}); // L2CAP ConnRsp refusal

    engine.ProcessControllerAclIn(MakeL2capPkt(kMouseHandle, 0x0001, rfcommConnReq), winAcl);
    QVERIFY(!winAcl.empty()); // Mouse RFCOMM forwarded untouched!
}

void RadioServiceProtocolTests::Uac1MicEmulatorDescriptorsAndAdaptiveAudio()
{
    Uac1MicEmulator mic{0xF433B7263664ull, "AirPods Pro 3", false};
    QCOMPARE(mic.Serial(), std::string{"APBMICF433B7263664"});
    QCOMPARE(mic.ProductString(), std::string{"A2DP, AirPods Pro 3"});

    // Validate Configuration Descriptor wTotalLength matches actual size.
    const auto &cfg = mic.ConfigDescriptor();
    QVERIFY(cfg.size() >= 9);
    const uint16_t declaredLen =
        static_cast<uint16_t>(cfg[2]) | static_cast<uint16_t>(static_cast<uint16_t>(cfg[3]) << 8);
    QCOMPARE(static_cast<size_t>(declaredLen), cfg.size());

    // GET_DESCRIPTOR(DEVICE) returns 18 bytes.
    const auto devReply =
        mic.HandleControlTransfer({0x80, 0x06, 0x00, 0x01, 0x00, 0x00, 18, 0}, {});
    QCOMPARE(devReply.status, UsbIp::kErrOk);
    QCOMPARE(devReply.data.size(), size_t{18});

    // SET_INTERFACE 1 alt 1 starts capture; alt 0 stops capture.
    bool captureState = false;
    mic.SetOnCaptureChanged([&](bool cap) { captureState = cap; });
    const auto startReply =
        mic.HandleControlTransfer({0x01, 0x0B, 0x01, 0x00, 0x01, 0x00, 0, 0}, {});
    QCOMPARE(startReply.status, UsbIp::kErrOk);
    QVERIFY(captureState);
    QVERIFY(mic.IsCapturing());

    // Push 30 ms of 1 kHz sine wave (1440 samples at 48 kHz) -> verify audio flows cleanly.
    std::vector<int16_t> tone(48 * 30);
    for (size_t i = 0; i < tone.size(); ++i) {
        const double t = static_cast<double>(i) / 48000.0;
        tone[i] = static_cast<int16_t>(std::sin(2.0 * 3.14159265358979 * 1000.0 * t) * 16000.0);
    }
    mic.PushPcm(0, tone);

    const auto iso = mic.FillIsochronousIn(10); // 10 ms = 960 bytes
    QCOMPARE(iso.packets.size(), size_t{10});
    QCOMPARE(iso.data.size(), size_t{10 * Uac1MicEmulator::kBytesPerMs});

    int peak = 0;
    for (size_t i = 0; i + 1 < iso.data.size(); i += 2) {
        const int16_t s = static_cast<int16_t>(
            static_cast<uint16_t>(iso.data[i]) | (static_cast<uint16_t>(iso.data[i + 1]) << 8));
        peak = std::max(peak, std::abs(static_cast<int>(s)));
    }
    QVERIFY(peak > 5000);

    // Feature Unit 2 Mute silences output.
    const auto muteReply = mic.HandleControlTransfer(
        {0x21, 0x01, 0x00, 0x01, 0x00, 0x02, 1, 0}, std::array<uint8_t, 1>{1});
    QCOMPARE(muteReply.status, UsbIp::kErrOk);
    const auto mutedIso = mic.FillIsochronousIn(2);
    for (uint8_t b : mutedIso.data) {
        QCOMPARE(b, uint8_t{0});
    }

    // SET_INTERFACE 1 alt 0 stops capture.
    mic.HandleControlTransfer({0x01, 0x0B, 0x00, 0x00, 0x01, 0x00, 0, 0}, {});
    QVERIFY(!captureState);
    QVERIFY(!mic.IsCapturing());
}

QTEST_MAIN(RadioServiceProtocolTests)
#include "RadioServiceProtocolTests.moc"
