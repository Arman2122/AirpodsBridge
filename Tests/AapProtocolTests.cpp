// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <vector>

#include <QtTest>

#include "Source/Domain/AapProtocol.h"
#include "Source/Domain/AirPodsStateManager.h"
#include "Source/Domain/RtBuddySensor.h"

using namespace Core::AirPods;
using namespace Core::AirPods::Aap;
using namespace std::chrono_literals;

class AapProtocolTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void BuildsHandshakeAndControlPackets();
    void BuildsRenamePacketWithLengthClamp();
    void DecodesExactBatteryNotification();
    void DecodesEarDetectionNotification();
    void DecodesNoiseControlAndConversationalAwareness();
    void DecodesExtendedH2SettingsAndMerges();
    void DecodesAdvancedAudioMicAndHealthControls();
    void DecodesMetadataNotification();
    void DecodesRealAirPodsPro3MetadataOffset11();
    void DecodesConversationalAwarenessSpeechLevelsAndClearsOnDisable();
    void DecodesRtBuddyHeartRateFramesAndDynamicDescriptors();
    void RejectsMalformedAndTruncatedPackets();
    void AppliesInboundMessagesAndMergesInStateManager();
};

void AapProtocolTests::BuildsHandshakeAndControlPackets()
{
    const auto handshake = BuildHandshakePacket();
    QCOMPARE(handshake.size(), size_t{16});
    QCOMPARE(handshake[0], uint8_t{0x00});
    QCOMPARE(handshake[4], uint8_t{0x01});

    const auto notify = BuildRequestNotificationsPacket();
    QCOMPARE(notify.size(), size_t{10});
    QCOMPARE(notify[4], uint8_t{0x0F});

    const auto features = BuildEnableFeaturesPacket();
    QCOMPARE(features.size(), size_t{14});
    QCOMPARE(features[4], uint8_t{0x4D});

    const auto anc = BuildSetNoiseControlPacket(NoiseControlMode::NoiseCancellation);
    QCOMPARE(anc.size(), size_t{11});
    QCOMPARE(anc[4], uint8_t{0x09});
    QCOMPARE(anc[6], uint8_t{0x0D});
    QCOMPARE(anc[7], uint8_t{0x02});

    const auto adaptive = BuildSetNoiseControlPacket(NoiseControlMode::Adaptive);
    QCOMPARE(adaptive[7], uint8_t{0x04});

    const auto caOn = BuildSetConversationalAwarenessPacket(true);
    QCOMPARE(caOn[6], uint8_t{0x28});
    QCOMPARE(caOn[7], uint8_t{0x01});

    const auto caOff = BuildSetConversationalAwarenessPacket(false);
    QCOMPARE(caOff[7], uint8_t{0x02});
}

void AapProtocolTests::BuildsRenamePacketWithLengthClamp()
{
    const auto rename = BuildSetRenamePacket("Studio Pods");
    QCOMPARE(rename.size(), size_t{8 + 11});
    QCOMPARE(rename[4], uint8_t{0x1A});
    QCOMPARE(rename[7], uint8_t{11});

    const std::string huge(300, 'A');
    const auto clamped = BuildSetRenamePacket(huge);
    QCOMPARE(clamped.size(), size_t{8 + 240});
    QCOMPARE(clamped[7], uint8_t{240});
}

void AapProtocolTests::DecodesExactBatteryNotification()
{
    // 3 components: Left 87% charging, Right 93% not charging, Case 41% not charging
    const std::vector<uint8_t> packet{
        0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x03, // header + count = 3
        0x04, 0x01, 87,   0x01, 0x01,             // Left: 87%, charging
        0x02, 0x01, 93,   0x02, 0x01,             // Right: 93%, not charging
        0x08, 0x01, 41,   0x02, 0x01,             // Case: 41%, not charging
    };

    const auto decoded = DecodePacket(packet);
    QVERIFY(decoded.has_value());
    const auto *battery = std::get_if<BatteryNotification>(&decoded.value());
    QVERIFY(battery != nullptr);
    QCOMPARE(battery->entries.size(), size_t{3});
    QCOMPARE(battery->entries[0].component, ComponentType::Left);
    QCOMPARE(battery->entries[0].level, uint8_t{87});
    QCOMPARE(battery->entries[0].status, ChargeStatus::Charging);
    QCOMPARE(battery->entries[1].component, ComponentType::Right);
    QCOMPARE(battery->entries[1].level, uint8_t{93});
    QCOMPARE(battery->entries[1].status, ChargeStatus::NotCharging);
    QCOMPARE(battery->entries[2].component, ComponentType::Case);
    QCOMPARE(battery->entries[2].level, uint8_t{41});
}

void AapProtocolTests::DecodesEarDetectionNotification()
{
    const std::vector<uint8_t> packet{0x04, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x02};
    const auto decoded = DecodePacket(packet);
    QVERIFY(decoded.has_value());

    const auto *ear = std::get_if<EarDetectionNotification>(&decoded.value());
    QVERIFY(ear != nullptr);
    QCOMPARE(ear->primary, EarPlacement::InEar);
    QCOMPARE(ear->secondary, EarPlacement::InCase);
}

void AapProtocolTests::DecodesNoiseControlAndConversationalAwareness()
{
    const auto transpPacket = BuildSetNoiseControlPacket(NoiseControlMode::Transparency);
    const auto decodedNoise = DecodePacket(transpPacket);
    QVERIFY(decodedNoise.has_value());
    const auto *noise = std::get_if<NoiseControlNotification>(&decodedNoise.value());
    QVERIFY(noise != nullptr);
    QCOMPARE(noise->mode, NoiseControlMode::Transparency);

    const auto awarenessPacket = BuildSetConversationalAwarenessPacket(true);
    const auto decodedAwareness = DecodePacket(awarenessPacket);
    QVERIFY(decodedAwareness.has_value());
    const auto *awareness =
        std::get_if<ConversationalAwarenessNotification>(&decodedAwareness.value());
    QVERIFY(awareness != nullptr);
    QVERIFY(awareness->enabled);
}

void AapProtocolTests::DecodesExtendedH2SettingsAndMerges()
{
    const auto oneBud = DecodePacket(BuildSetOneBudAncPacket(true));
    QVERIFY(oneBud.has_value());
    const auto *oneBudMsg = std::get_if<FeatureToggleNotification>(&oneBud.value());
    QVERIFY(oneBudMsg != nullptr);
    QCOMPARE(oneBudMsg->feature, ToggleFeature::OneBudAnc);
    QVERIFY(oneBudMsg->enabled);

    const auto adaptiveLevel = DecodePacket(BuildSetAdaptiveNoiseLevelPacket(72));
    QVERIFY(adaptiveLevel.has_value());
    const auto *levelMsg = std::get_if<AdaptiveNoiseLevelNotification>(&adaptiveLevel.value());
    QVERIFY(levelMsg != nullptr);
    QCOMPARE(levelMsg->level, uint8_t{72});

    const auto headGestures = DecodePacket(BuildSetHeadGesturesPacket(true));
    QVERIFY(headGestures.has_value());

    State st;
    st.model = Model::AirPods_Pro_3;
    QVERIFY(ApplyInboundMessage(st, oneBud.value()));
    QVERIFY(ApplyInboundMessage(st, adaptiveLevel.value()));
    QVERIFY(ApplyInboundMessage(st, headGestures.value()));
    QCOMPARE(st.noiseControl.oneBudAnc, std::optional{true});
    QCOMPARE(st.noiseControl.adaptiveNoiseLevel, std::optional<uint8_t>{72});
    QCOMPARE(st.noiseControl.headGestures, std::optional{true});
}

void AapProtocolTests::DecodesAdvancedAudioMicAndHealthControls()
{
    const auto micLeft = DecodePacket(BuildSetMicrophoneSidePacket(MicrophoneSide::AlwaysLeft));
    QVERIFY(micLeft.has_value());
    const auto *micMsg = std::get_if<MicrophoneSideNotification>(&micLeft.value());
    QVERIFY(micMsg != nullptr);
    QCOMPARE(micMsg->side, MicrophoneSide::AlwaysLeft);

    const auto tone = DecodePacket(BuildSetToneVolumePacket(65));
    QVERIFY(tone.has_value());
    const auto *toneMsg = std::get_if<ToneVolumeNotification>(&tone.value());
    QVERIFY(toneMsg != nullptr);
    QCOMPARE(toneMsg->volume, uint8_t{65});

    const auto allowOff = DecodePacket(BuildSetAllowOffListeningModePacket(true));
    QVERIFY(allowOff.has_value());
    const auto hearingAid = DecodePacket(BuildSetHearingAidPacket(true));
    QVERIFY(hearingAid.has_value());
    const auto mediaAssist = DecodePacket(BuildSetMediaAssistPacket(true));
    QVERIFY(mediaAssist.has_value());

    const uint8_t cycleMask = ListeningCycleBit::kNoiseCancellation |
                              ListeningCycleBit::kTransparency | ListeningCycleBit::kAdaptive;
    const auto cycle = DecodePacket(BuildSetListeningModeCyclePacket(cycleMask));
    QVERIFY(cycle.has_value());
    const auto pressSpeed = DecodePacket(BuildSetPressSpeedPacket(PressSpeed::Slower));
    QVERIFY(pressSpeed.has_value());
    const auto holdDur = DecodePacket(BuildSetPressHoldDurationPacket(PressHoldDuration::Shorter));
    QVERIFY(holdDur.has_value());

    // Conversational Awareness active speaking notification (real 10-byte opcode 0x4B; byte 9 =
    // level).
    const std::vector<uint8_t> speechPkt{0x04, 0x00, 0x04, 0x00, 0x4B,
                                         0x00, 0x02, 0x00, 0x01, 0x01};
    const auto speech = DecodePacket(speechPkt);
    QVERIFY(speech.has_value());
    const auto *speechMsg = std::get_if<ConversationalAwarenessSpeechNotification>(&speech.value());
    QVERIFY(speechMsg != nullptr);
    QVERIFY(speechMsg->speaking);
    QCOMPARE(speechMsg->level, uint8_t{1});

    // Heart rate telemetry notification (AirPods Pro 3, 74 BPM) + real HRM_STATE (0x30) + RTBuddy
    // 0x17.
    const auto hrToggle = DecodePacket(BuildSetHeartRateMonitoringPacket(true));
    QVERIFY(hrToggle.has_value());
    const auto *hrToggleMsg = std::get_if<FeatureToggleNotification>(&hrToggle.value());
    QVERIFY(hrToggleMsg != nullptr);
    QCOMPARE(hrToggleMsg->feature, ToggleFeature::HeartRateMonitoring);
    QVERIFY(hrToggleMsg->enabled);

    // 0x00 boolean false encoding is accepted alongside 0x02 on real HRM_STATE setting 0x30.
    const std::vector<uint8_t> hrOffZero{0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x30, 0x00, 0, 0, 0};
    const auto hrOffDecoded = DecodePacket(hrOffZero);
    QVERIFY(hrOffDecoded.has_value());
    QVERIFY(!std::get<FeatureToggleNotification>(hrOffDecoded.value()).enabled);

    const auto hrReq = BuildRequestHeartRatePacket();
    QCOMPARE(hrReq[4], uint8_t{0x17});
    QCOMPARE(hrReq.size(), size_t{29});

    const std::vector<uint8_t> hrPkt{0x04, 0x00, 0x04, 0x00, 0x62, 0x00, 0x00, 74, 0x00};
    const auto hr = DecodePacket(hrPkt);
    QVERIFY(hr.has_value());
    const auto *hrMsg = std::get_if<HeartRateNotification>(&hr.value());
    QVERIFY(hrMsg != nullptr);
    QCOMPARE(hrMsg->bpm, uint16_t{74});

    // Compact 8-byte heart rate frame also decodes cleanly.
    const std::vector<uint8_t> hrCompact{0x04, 0x00, 0x04, 0x00, 0x62, 0x00, 0x00, 88};
    const auto hr2 = DecodePacket(hrCompact);
    QVERIFY(hr2.has_value());
    QCOMPARE(std::get<HeartRateNotification>(hr2.value()).bpm, uint16_t{88});

    // Sideband mic start/stop frames.
    const auto startMic = BuildStartSidebandMicPacket();
    const auto stopMic = BuildStopSidebandMicPacket();
    QCOMPARE(startMic[4], uint8_t{0x58});
    QCOMPARE(startMic[8], uint8_t{0x01});
    QCOMPARE(stopMic[8], uint8_t{0x02});

    // Merge everything into State.
    State st;
    st.model = Model::AirPods_Pro_3;
    QVERIFY(ApplyInboundMessage(st, micLeft.value()));
    QVERIFY(ApplyInboundMessage(st, tone.value()));
    QVERIFY(ApplyInboundMessage(st, allowOff.value()));
    QVERIFY(ApplyInboundMessage(st, hearingAid.value()));
    QVERIFY(ApplyInboundMessage(st, mediaAssist.value()));
    QVERIFY(ApplyInboundMessage(st, cycle.value()));
    QVERIFY(ApplyInboundMessage(st, pressSpeed.value()));
    QVERIFY(ApplyInboundMessage(st, holdDur.value()));
    QVERIFY(ApplyInboundMessage(st, speech.value()));
    QVERIFY(ApplyInboundMessage(st, hrToggle.value()));
    QVERIFY(ApplyInboundMessage(st, hr.value()));
    QVERIFY(ApplyInboundMessage(st, hr2.value()));

    QCOMPARE(st.noiseControl.microphoneSide, std::optional{MicrophoneSide::AlwaysLeft});
    QCOMPARE(st.noiseControl.toneVolume, std::optional<uint8_t>{65});
    QCOMPARE(st.noiseControl.allowOffListeningMode, std::optional{true});
    QCOMPARE(st.noiseControl.hearingAidEnabled, std::optional{true});
    QCOMPARE(st.noiseControl.mediaAssistEnabled, std::optional{true});
    QCOMPARE(st.noiseControl.listeningModeCycleMask, std::optional<uint8_t>{cycleMask});
    QCOMPARE(st.noiseControl.pressSpeed, std::optional{PressSpeed::Slower});
    QCOMPARE(st.noiseControl.pressHoldDuration, std::optional{PressHoldDuration::Shorter});
    QCOMPARE(st.noiseControl.conversationalAwarenessSpeaking, std::optional{true});
    QCOMPARE(st.noiseControl.heartRateMonitoringEnabled, std::optional{true});
    QCOMPARE(st.noiseControl.heartRateBpm, std::optional<uint16_t>{88});
    QCOMPARE(st.noiseControl.heartRateMinBpm, std::optional<uint16_t>{74});
    QCOMPARE(st.noiseControl.heartRateMaxBpm, std::optional<uint16_t>{88});
    QCOMPARE(st.noiseControl.heartRateHistory.size(), size_t{2});
}

void AapProtocolTests::DecodesMetadataNotification()
{
    std::vector<uint8_t> packet{0x04, 0x00, 0x04, 0x00, 0x1D, 0x00,
                                0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    const auto appendStr = [&](const char *s) {
        while (*s != '\0') {
            packet.push_back(static_cast<uint8_t>(*s++));
        }
        packet.push_back(0);
    };
    appendStr("Arman's AirPods Pro");
    appendStr("A3047");
    appendStr("Apple Inc.");
    appendStr("SERIAL123");
    appendStr("7B21");

    const auto decoded = DecodePacket(packet);
    QVERIFY(decoded.has_value());
    const auto *meta = std::get_if<MetadataNotification>(&decoded.value());
    QVERIFY(meta != nullptr);
    QCOMPARE(meta->deviceName, QString{"Arman's AirPods Pro"});
    QCOMPARE(meta->modelNumber, QString{"A3047"});
    QCOMPARE(meta->firmwareVersion, QString{"7B21"});
}

void AapProtocolTests::DecodesRealAirPodsPro3MetadataOffset11()
{
    // Real Apple AirPods Pro 3 (A3063) 0x1D frame has a 5-byte metadata header after the
    // 6-byte AAP prefix, so deviceName starts at byte 11 (must not clip 'A' -> "irPods Pro").
    std::vector<uint8_t> packet{
        0x04, 0x00, 0x04, 0x00, 0x1D, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
    };
    const auto appendStr = [&](const char *s) {
        while (*s != '\0') {
            packet.push_back(static_cast<uint8_t>(*s++));
        }
        packet.push_back(0);
    };
    appendStr("AirPods Pro");
    appendStr("A3063");
    appendStr("Apple Inc.");
    appendStr("G0X123456789");
    appendStr("91.3431046004000000.29");

    const auto decoded = DecodePacket(packet);
    QVERIFY(decoded.has_value());
    const auto *meta = std::get_if<MetadataNotification>(&decoded.value());
    QVERIFY(meta != nullptr);
    QCOMPARE(meta->deviceName, QString{"AirPods Pro"});
    QCOMPARE(meta->modelNumber, QString{"A3063"});
    QCOMPARE(meta->manufacturer, QString{"Apple Inc."});
    QCOMPARE(meta->serialNumber, QString{"G0X123456789"});
    QCOMPARE(meta->firmwareVersion, QString{"91.3431046004000000.29"});

    // Applying metadata with a known Apple model number upgrades Unknown state.model.
    State unknownState;
    QVERIFY(ApplyInboundMessage(unknownState, decoded.value()));
    QCOMPARE(unknownState.model, Model::AirPods_Pro_3);
    QCOMPARE(unknownState.displayName, QString{"AirPods Pro"});
}

void AapProtocolTests::DecodesConversationalAwarenessSpeechLevelsAndClearsOnDisable()
{
    // Truncated 9-byte packet (missing byte 9 level) must be rejected so byte 8 == 0x01
    // never falsely latches speaking=true.
    QVERIFY(
        !DecodePacket(std::vector<uint8_t>{0x04, 0x00, 0x04, 0x00, 0x4B, 0x00, 0x02, 0x00, 0x01})
             .has_value());

    // Levels 1 and 2 => speaking=true; 3..7 => recovering (speaking=false); 8..9 => normal.
    for (uint8_t lvl : {uint8_t{1}, uint8_t{2}}) {
        const auto d = DecodePacket(
            std::vector<uint8_t>{0x04, 0x00, 0x04, 0x00, 0x4B, 0x00, 0x02, 0x00, 0x01, lvl});
        QVERIFY(d.has_value());
        const auto &m = std::get<ConversationalAwarenessSpeechNotification>(d.value());
        QVERIFY(m.speaking);
        QCOMPARE(m.level, lvl);
    }
    for (uint8_t lvl : {uint8_t{3}, uint8_t{5}, uint8_t{8}, uint8_t{9}}) {
        const auto d = DecodePacket(
            std::vector<uint8_t>{0x04, 0x00, 0x04, 0x00, 0x4B, 0x00, 0x02, 0x00, 0x01, lvl});
        QVERIFY(d.has_value());
        const auto &m = std::get<ConversationalAwarenessSpeechNotification>(d.value());
        QVERIFY(!m.speaking);
        QCOMPARE(m.level, lvl);
    }

    // Disabling Conversation Awareness immediately clears any active speaking state.
    State st;
    st.model = Model::AirPods_Pro_3;
    QVERIFY(ApplyInboundMessage(st, ConversationalAwarenessNotification{.enabled = true}));
    QVERIFY(ApplyInboundMessage(
        st, ConversationalAwarenessSpeechNotification{.speaking = true, .level = 1}));
    QCOMPARE(st.noiseControl.conversationalAwarenessSpeaking, std::optional{true});
    QCOMPARE(st.noiseControl.conversationalAwarenessLevel, uint8_t{1});

    QVERIFY(ApplyInboundMessage(st, ConversationalAwarenessNotification{.enabled = false}));
    QCOMPARE(st.noiseControl.conversationalAwarenessSpeaking, std::optional{false});
    QCOMPARE(st.noiseControl.conversationalAwarenessLevel, uint8_t{0});

    // Inbound speech frames while CA is explicitly off must not re-assert speaking=true.
    QVERIFY(!ApplyInboundMessage(
        st, ConversationalAwarenessSpeechNotification{.speaking = true, .level = 1}));
    QCOMPARE(st.noiseControl.conversationalAwarenessSpeaking, std::optional{false});
}

void AapProtocolTests::DecodesRtBuddyHeartRateFramesAndDynamicDescriptors()
{
    // Helper to build a SensorDataWX 0x17 frame wrapping a protobuf body.
    const auto wrapRtBuddy = [](const std::vector<uint8_t> &body) {
        std::vector<uint8_t> frame{
            0x04,
            0x00,
            0x04,
            0x00,
            0x17,
            0x00,
            0x00,
            0x00,
            0x10,
            0x00,
            static_cast<uint8_t>(body.size() & 0xFFu),
            static_cast<uint8_t>((body.size() >> 8) & 0xFFu),
        };
        frame.insert(frame.end(), body.begin(), body.end());
        return frame;
    };

    const auto appendVarint = [](std::vector<uint8_t> &out, uint64_t val) {
        do {
            uint8_t b = static_cast<uint8_t>(val & 0x7Fu);
            val >>= 7;
            if (val != 0) {
                b |= 0x80u;
            }
            out.push_back(b);
        } while (val != 0);
    };

    const auto makeHrCommandFrame = [&](uint8_t service, uint8_t bpm, uint8_t confidence,
                                        uint8_t t0, uint8_t t1, uint8_t t2, uint8_t logType = 3) {
        std::vector<uint8_t> payload(18, 0);
        payload[1] = bpm;
        payload[2] = confidence;
        payload[15] = t0;
        payload[16] = t1;
        payload[17] = t2;

        // SensorCommand: field 1 = service, field 3 = payload(18)
        std::vector<uint8_t> cmd;
        cmd.push_back(0x08);
        cmd.push_back(service);
        cmd.push_back(0x1A);
        cmd.push_back(18);
        cmd.insert(cmd.end(), payload.begin(), payload.end());

        // SensorDataWX: field 1 = seq(3), field 2 = log_type, field 7 = command
        std::vector<uint8_t> body{0x08, 0x03, 0x10, logType, 0x3A};
        appendVarint(body, cmd.size());
        body.insert(body.end(), cmd.begin(), cmd.end());
        return wrapRtBuddy(body);
    };

    // Outbound wire sequence sanity checks.
    QCOMPARE(RtBuddy::BuildConnectService0Packet().size(), size_t{16});
    QCOMPARE(RtBuddy::BuildCapabilitiesService0Packet().size(), size_t{7});
    QCOMPARE(RtBuddy::BuildConnectService4Packet().size(), size_t{16});
    QCOMPARE(RtBuddy::BuildCapabilitiesService4Packet().size(), size_t{7});
    const auto hrmOn = RtBuddy::BuildHrmStatePacket(true);
    QCOMPARE(hrmOn[6], uint8_t{0x30});
    QCOMPARE(hrmOn[7], uint8_t{0x01});
    const auto reqDesc = RtBuddy::BuildRequestAllDescriptorsPacket(1);
    QCOMPARE(reqDesc.size(), size_t{16});
    const auto stream1Hz = RtBuddy::BuildServiceReportIntervalPacket(
        2, RtBuddy::kLegacyHeartRateCommandService, 1000000u);
    QCOMPARE(stream1Hz.size(), size_t{29});
    QCOMPARE(stream1Hz[19], uint8_t{84});
    QCOMPARE(stream1Hz[25], uint8_t{0x40});
    QCOMPARE(stream1Hz[26], uint8_t{0x42});
    QCOMPARE(stream1Hz[27], uint8_t{0x0F});
    QCOMPARE(stream1Hz[28], uint8_t{0x00});

    // All 6 captured hardware status tails decode cleanly on legacy services (19, 20, 84).
    const std::array<std::array<uint8_t, 3>, 6> validTails{{
        {0x10, 0x00, 0x00},
        {0x10, 0x00, 0x80},
        {0x20, 0x00, 0x00},
        {0x20, 0x80, 0x00},
        {0x20, 0x02, 0x80},
        {0x20, 0x82, 0x80},
    }};
    RtBuddy::SessionParser parser;
    for (const auto &tail : validTails) {
        const auto frame = makeHrCommandFrame(20, 72, 0xB4, tail[0], tail[1], tail[2]);
        const auto batch = parser.ParseFrame(frame);
        QVERIFY(batch.heartRate.has_value());
        QCOMPARE(batch.heartRate->bpm, uint16_t{72});
        QCOMPARE(batch.heartRate->confidence, uint8_t{0xB4});
    }

    // Warm-up / unlocked frames (confidence < 0x80 or non-locked tail) report warmupFrameSeen
    // without polluting BPM.
    const auto lowConf = makeHrCommandFrame(19, 169, 0x40, 0x10, 0x00, 0x80);
    const auto lowConfBatch = parser.ParseFrame(lowConf);
    QVERIFY(!lowConfBatch.heartRate.has_value());
    QVERIFY(lowConfBatch.warmupFrameSeen);

    const auto badTail = makeHrCommandFrame(19, 137, 0xC0, 0x01, 0x02, 0x03);
    const auto badTailBatch = parser.ParseFrame(badTail);
    QVERIFY(!badTailBatch.heartRate.has_value());
    QVERIFY(badTailBatch.warmupFrameSeen);

    // FrameReassembler reconstructs split chunks cleanly across arbitrary boundaries.
    const auto fullFrame = makeHrCommandFrame(84, 78, 0xE0, 0x20, 0x00, 0x00);
    RtBuddy::FrameReassembler reassembler;
    const auto part1 = reassembler.Feed(std::span<const uint8_t>{fullFrame}.subspan(0, 15));
    QVERIFY(part1.empty());
    const auto part2 = reassembler.Feed(std::span<const uint8_t>{fullFrame}.subspan(15));
    QCOMPARE(part2.size(), size_t{1});
    QCOMPARE(part2[0], fullFrame);

    // Direct DecodePacket also decodes single-frame RTBuddy 0x17 heart-rate samples.
    const auto decoded = DecodePacket(fullFrame);
    QVERIFY(decoded.has_value());
    const auto *hrMsg = std::get_if<HeartRateNotification>(&decoded.value());
    QVERIFY(hrMsg != nullptr);
    QCOMPARE(hrMsg->bpm, uint16_t{78});
    QCOMPARE(hrMsg->confidence, uint8_t{0xE0});

    // Dynamic HID descriptor dictionary discovery remaps heart rate to announced service 27
    // and retires legacy services 19/20/84 for this session.
    const auto makeBinaryDict = [&](uint64_t vendorId, const char *name, uint64_t maxReportSize,
                                    const std::vector<uint8_t> &hidDesc) {
        std::vector<uint8_t> out{0xD3, 0x00, 0x00, 0x00};
        auto pushWord = [&](uint32_t w) {
            out.push_back(static_cast<uint8_t>(w & 0xFFu));
            out.push_back(static_cast<uint8_t>((w >> 8) & 0xFFu));
            out.push_back(static_cast<uint8_t>((w >> 16) & 0xFFu));
            out.push_back(static_cast<uint8_t>((w >> 24) & 0xFFu));
        };
        auto pushPadded = [&](const uint8_t *data, size_t n) {
            out.insert(out.end(), data, data + n);
            while ((out.size() & 3u) != 0) {
                out.push_back(0);
            }
        };
        auto pushItem = [&](uint8_t kind, uint32_t count, const std::vector<uint8_t> &val) {
            pushWord((static_cast<uint32_t>(kind) << 24) | (count & 0x00FFFFFFu));
            pushPadded(val.data(), val.size());
        };
        auto pushNum = [&](const char *key, uint64_t v) {
            std::vector<uint8_t> kb(key, key + std::strlen(key) + 1);
            pushItem(9, static_cast<uint32_t>(kb.size()), kb);
            std::vector<uint8_t> nb(8);
            for (int i = 0; i < 8; ++i) {
                nb[static_cast<size_t>(i)] = static_cast<uint8_t>((v >> (i * 8)) & 0xFFu);
            }
            pushItem(4, 64, nb);
        };
        auto pushStr = [&](const char *key, const char *val) {
            std::vector<uint8_t> kb(key, key + std::strlen(key) + 1);
            pushItem(9, static_cast<uint32_t>(kb.size()), kb);
            std::vector<uint8_t> vb(val, val + std::strlen(val) + 1);
            pushItem(9, static_cast<uint32_t>(vb.size()), vb);
        };
        // Dictionary header: 4 entries.
        pushWord(0x01000004u);
        pushNum("VendorID", vendorId);
        pushStr("HeartRateService", name);
        pushNum("MaxReportSize", maxReportSize);
        std::vector<uint8_t> descKey{'R', 'e', 'p', 'o', 'r', 't', 'D', 'e', 's',
                                     'c', 'r', 'i', 'p', 't', 'o', 'r', 0};
        pushItem(9, static_cast<uint32_t>(descKey.size()), descKey);
        pushItem(10, static_cast<uint32_t>(hidDesc.size()), hidDesc);
        return out;
    };

    const std::vector<uint8_t> heartHid{
        0x05, 0x20, 0x09, 0x16, 0xa1, 0x01, 0x85, 0x01, 0x0a, 0x0e, 0x03, 0x14, 0x27, 0xff,
        0xff, 0xff, 0x7f, 0x75, 0x20, 0x95, 0x01, 0xb1, 0x02, 0x0a, 0xb8, 0x04, 0x26, 0xff,
        0x00, 0x75, 0x08, 0x95, 0x01, 0x81, 0x02, 0x06, 0x15, 0xff, 0x0a, 0x20, 0x01, 0x95,
        0x01, 0x75, 0x08, 0x81, 0x02, 0x26, 0xff, 0x7f, 0x06, 0x15, 0xff, 0x0a, 0x21, 0x01,
        0x95, 0x01, 0x75, 0x10, 0x81, 0x02, 0x75, 0x08, 0x95, 0x01, 0x15, 0x01, 0x25, 0x02,
        0x1a, 0x04, 0x01, 0x2a, 0x05, 0x01, 0x81, 0x00, 0x06, 0x00, 0xff, 0x09, 0x23, 0xa1,
        0x00, 0x15, 0x00, 0x24, 0x06, 0x15, 0xff, 0x09, 0x04, 0x95, 0x08, 0x75, 0x08, 0x81,
        0x02, 0x06, 0x0a, 0xff, 0x09, 0x12, 0x95, 0x04, 0x75, 0x08, 0x81, 0x02, 0x85, 0x02,
        0x06, 0x0a, 0xff, 0x09, 0x13, 0x96, 0x58, 0x02, 0x75, 0x08, 0x81, 0x02, 0xc0, 0xc0,
    };
    const auto hrDict = makeBinaryDict(1452, "HeartRate", 601, heartHid);
    QCOMPARE(
        RtBuddy::ParseSensorDescriptorKind(hrDict),
        std::optional{RtBuddy::DescriptorKind::HeartRate});
    QVERIFY(!RtBuddy::ParseSensorDescriptorKind(makeBinaryDict(1, "HeartRate", 601, heartHid))
                 .has_value());

    // Wrap descriptor announcement for service 27 inside SensorDataWX field 5.
    std::vector<uint8_t> sdMsg{0x08, 27, 0x12};
    appendVarint(sdMsg, hrDict.size());
    sdMsg.insert(sdMsg.end(), hrDict.begin(), hrDict.end());
    std::vector<uint8_t> annBody{0x08, 0x01, 0x10, 0x03, 0x2A};
    appendVarint(annBody, sdMsg.size());
    annBody.insert(annBody.end(), sdMsg.begin(), sdMsg.end());

    const auto annBatch = parser.ParseFrame(wrapRtBuddy(annBody));
    QVERIFY(annBatch.mappingChanged);
    QCOMPARE(parser.HeartRateService(), std::optional<uint8_t>{27});
    QVERIFY(!parser.ParseFrame(makeHrCommandFrame(20, 72, 0xB4, 0x10, 0x00, 0x80))
                 .heartRate.has_value());
    const auto remapped = parser.ParseFrame(makeHrCommandFrame(27, 68, 0xD0, 0x20, 0x00, 0x00));
    QVERIFY(remapped.heartRate.has_value());
    QCOMPARE(remapped.heartRate->bpm, uint16_t{68});
}

void AapProtocolTests::RejectsMalformedAndTruncatedPackets()
{
    QVERIFY(!DecodePacket({}).has_value());
    QVERIFY(!DecodePacket(std::vector<uint8_t>{0x04, 0x00, 0x04}).has_value());
    // Wrong header prefix
    QVERIFY(!DecodePacket(std::vector<uint8_t>{0x00, 0x00, 0x04, 0x00, 0x06, 0x00, 0x00, 0x00})
                 .has_value());
    // Battery count exceeds available bytes
    QVERIFY(!DecodePacket(std::vector<uint8_t>{0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x03, 0x04})
                 .has_value());
    // Out-of-range battery percentage (101)
    QVERIFY(
        !DecodePacket(std::vector<uint8_t>{
                          0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, 0x04, 0x01, 101, 0x02, 0x01})
             .has_value());
    // Invalid noise control mode byte (0x09)
    QVERIFY(!DecodePacket(std::vector<uint8_t>{0x04, 0x00, 0x04, 0x00, 0x09, 0x00, 0x0D, 0x09})
                 .has_value());
}

void AapProtocolTests::AppliesInboundMessagesAndMergesInStateManager()
{
    Details::StateManager manager;
    const auto now = Details::StateManager::Clock::now();

    QVERIFY(!manager.SetAapConnected(true).has_value());

    const auto batteryMsg = DecodePacket(std::vector<uint8_t>{
        0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x02, // 2 components
        0x04, 0x01, 83, 0x01, 0x01,               // Left: 83% charging
        0x02, 0x01, 79, 0x02, 0x01,               // Right: 79% discharging
    });
    QVERIFY(batteryMsg.has_value());

    const auto update1 = manager.OnAapMessage(batteryMsg.value(), Model::AirPods_Pro_2, now);
    QVERIFY(update1.has_value());
    QCOMPARE(update1->newState.model, Model::AirPods_Pro_2);
    QCOMPARE(update1->newState.batteryPrecision, BatteryPrecision::ExactPercent);
    QCOMPARE(update1->newState.pods.left.battery.Value(), 83U);
    QVERIFY(update1->newState.pods.left.isCharging);
    QCOMPARE(update1->newState.pods.right.battery.Value(), 79U);
    QVERIFY(!update1->newState.pods.right.isCharging);
    QVERIFY(update1->newState.noiseControl.controllable);

    // While AAP owns liveness, BLE timeout expiry must not arm a timer or discard state.
    QVERIFY(!manager.NextExpiry(now).has_value());
    QVERIFY(!manager.ExpireStale(now + 60s));
    QVERIFY(manager.GetCurrentState().has_value());

    // Noise control update propagates cleanly.
    const auto noiseMsg = DecodePacket(BuildSetNoiseControlPacket(NoiseControlMode::Adaptive));
    QVERIFY(noiseMsg.has_value());
    const auto update2 = manager.OnAapMessage(noiseMsg.value(), Model::AirPods_Pro_2, now + 1s);
    QVERIFY(update2.has_value());
    QCOMPARE(update2->newState.noiseControl.activeMode, std::optional{NoiseControlMode::Adaptive});

    // Audio profile propagates cleanly.
    const auto update3 = manager.SetAudioProfile(AudioProfile::HandsFree);
    QVERIFY(update3.has_value());
    QCOMPARE(update3->newState.audioProfile, AudioProfile::HandsFree);

    // Disconnecting AAP drops controllable flag without wiping battery state.
    const auto update4 = manager.SetAapConnected(false);
    QVERIFY(update4.has_value());
    QVERIFY(!update4->newState.noiseControl.controllable);
    QVERIFY(!update4->newState.noiseControl.activeMode.has_value());

    // Connecting AAP on a fresh manager does not fabricate an empty-battery state before live
    // packets arrive; once exact 1% battery (including case) arrives, controllable turns on
    // and closing the case lid (Disconnected status) preserves the last known 1% case level.
    Details::StateManager freshManager;
    QVERIFY(!freshManager.SetAapConnected(true, Model::AirPods_Pro_3).has_value());

    const auto fullBatt = DecodePacket(std::vector<uint8_t>{
        0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x03, // 3 components
        0x04, 0x01, 91,   0x00, 0x01,             // Left: 91%, status 0x00 (not charging)
        0x02, 0x01, 88,   0x02, 0x01,             // Right: 88%, not charging
        0x08, 0x01, 64,   0x01, 0x01,             // Case: 64%, charging
    });
    QVERIFY(fullBatt.has_value());
    const auto uBatt = freshManager.OnAapMessage(fullBatt.value(), Model::AirPods_Pro_3, now);
    QVERIFY(uBatt.has_value());
    QVERIFY(uBatt->newState.noiseControl.controllable);
    QCOMPARE(uBatt->newState.pods.left.battery.Value(), 91U);
    QCOMPARE(uBatt->newState.pods.right.battery.Value(), 88U);
    QCOMPARE(uBatt->newState.caseBox.battery.Value(), 64U);
    QVERIFY(uBatt->newState.caseBox.isCharging);
    QVERIFY(uBatt->newState.caseBox.isLidOpened);

    // Case lid closes -> AirPods send Case status 0x04 (Disconnected); 64% stays visible,
    // while `isLidOpened` turns false cleanly!
    const auto caseClosed = DecodePacket(std::vector<uint8_t>{
        0x04, 0x00, 0x04, 0x00, 0x04, 0x00, 0x01, // 1 component
        0x08, 0x01, 0x7F, 0x04, 0x01,             // Case: disconnected
    });
    QVERIFY(caseClosed.has_value());
    const auto uClosed =
        freshManager.OnAapMessage(caseClosed.value(), Model::AirPods_Pro_3, now + 2s);
    QVERIFY(uClosed.has_value());
    QVERIFY(uClosed->newState.caseBox.battery.Available());
    QCOMPARE(uClosed->newState.caseBox.battery.Value(), 64U);
    QVERIFY(!uClosed->newState.caseBox.isCharging);
    QVERIFY(!uClosed->newState.caseBox.isLidOpened);
}

QTEST_GUILESS_MAIN(AapProtocolTests)

#include "AapProtocolTests.moc"
