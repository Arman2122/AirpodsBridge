// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include <QtTest>

#include "Source/Core/AirPodsTransport_win.h"
#include "Source/Core/AudioControl.h"
#include "Source/Core/GlobalHotkeys.h"
#include "Source/Core/Ipc.h"
#include "Source/Core/Settings.h"
#include "Source/Domain/Base.h"
#include "Source/Support/Opts.h"

namespace {

class FakeControlTransport final : public Core::AirPods::ControlTransport
{
public:
    FakeControlTransport(std::string name, bool available)
        : _name{std::move(name)}, _available{available}
    {
    }

    bool IsAvailable() const override
    {
        return _available;
    }
    bool IsConnected() const override
    {
        return _connected;
    }
    std::string BackendName() const override
    {
        return _name;
    }
    void Connect(uint64_t addr) override
    {
        lastAddress = addr;
        _connected = true;
        if (_onConn) {
            _onConn(true);
        }
    }
    void Disconnect() override
    {
        if (_connected) {
            _connected = false;
            if (_onConn) {
                _onConn(false);
            }
        }
    }
    bool SendPacket(std::span<const uint8_t> pkt) override
    {
        sentPackets.emplace_back(pkt.begin(), pkt.end());
        return _connected;
    }
    void SetOnPacketReceived(PacketCallback cb) override
    {
        _onPkt = std::move(cb);
    }
    void SetOnConnectionChanged(ConnectionCallback cb) override
    {
        _onConn = std::move(cb);
    }

    uint64_t lastAddress{0};
    std::vector<std::vector<uint8_t>> sentPackets;

private:
    std::string _name;
    bool _available{false};
    bool _connected{false};
    PacketCallback _onPkt;
    ConnectionCallback _onConn;
};

} // namespace

class ProServicesTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ParsesAndFormatsHotkeys();
    void RejectsInvalidHotkeys();
    void MapsProductIdsToModels();
    void LatencyDiagnosticsAndDefaults();
    void ParsesElevatedNativeLatencyProfileOpts();
    void CompositeTransportPrefersPrimaryAndFallsBack();
    void NamedPipeIpcRoundTrips();
};

void ProServicesTests::ParsesAndFormatsHotkeys()
{
    using namespace Core::GlobalHotkeys;

    const auto a = ParseHotkey("ctrl+alt+a");
    QVERIFY(a.has_value());
    QVERIFY(a->ctrl);
    QVERIFY(a->alt);
    QVERIFY(!a->shift);
    QVERIFY(!a->win);
    QCOMPARE(a->virtualKey, uint32_t{'A'});
    QCOMPARE(FormatHotkey(a.value()), QStringLiteral("Ctrl+Alt+A"));

    const auto f9 = ParseHotkey("F9");
    QVERIFY(f9.has_value());
    QCOMPARE(FormatHotkey(f9.value()), QStringLiteral("F9"));

    const auto combo = ParseHotkey("Shift + Win + F12");
    QVERIFY(combo.has_value());
    QCOMPARE(FormatHotkey(combo.value()), QStringLiteral("Shift+Win+F12"));
}

void ProServicesTests::RejectsInvalidHotkeys()
{
    using namespace Core::GlobalHotkeys;

    QVERIFY(!ParseHotkey("").has_value());
    QVERIFY(!ParseHotkey("   ").has_value());
    // Bare letter without modifier is rejected so user cannot hijack normal typing.
    QVERIFY(!ParseHotkey("A").has_value());
    // Modifier-only or double-key is rejected.
    QVERIFY(!ParseHotkey("Ctrl+Alt").has_value());
    QVERIFY(!ParseHotkey("Ctrl+A+B").has_value());
    QVERIFY(!ParseHotkey("Ctrl+NotAKey").has_value());
}

void ProServicesTests::MapsProductIdsToModels()
{
    using Core::AirPods::Model;
    using Core::AirPods::ModelFromProductId;

    QCOMPARE(ModelFromProductId(0x2027), Model::AirPods_Pro_3);
    QCOMPARE(ModelFromProductId(0x2024), Model::AirPods_Pro_2_USB_C);
    QCOMPARE(ModelFromProductId(0x201F), Model::AirPods_Max_USB_C);
    QCOMPARE(ModelFromProductId(0x201B), Model::AirPods_4_ANC);
    QCOMPARE(ModelFromProductId(0xFFFF), Model::Unknown);
}

void ProServicesTests::LatencyDiagnosticsAndDefaults()
{
    // Idle CPU constraint: keep-alive must stay off by default until opted in.
    const auto defaults = Core::Settings::GetDefault();
    QVERIFY(!defaults.audio_keep_alive);

    Core::AudioControl::LatencyDiagnostics diag;
    QCOMPARE(diag.codec, QStringLiteral("AAC"));
    QVERIFY(!diag.IsLowLatencyProfile());

    diag.aacEnabled = false;
    diag.codec = QStringLiteral("SBC");
    QVERIFY(!diag.IsLowLatencyProfile());

    diag.enhancementsEnabled = false;
    QVERIFY(diag.IsLowLatencyProfile());
}

void ProServicesTests::ParsesElevatedNativeLatencyProfileOpts()
{
    std::string arg0 = "AirpodsBridge.exe";
    std::string arg1 = "--apply-latency-profile";
    std::string arg2 = "sbc";
    std::string arg3 = "--endpoint-guid";
    std::string arg4 = "{3eb939f5-52b0-4655-ab0e-981f1f51ab83}";
    std::string arg5 = "--container-id";
    std::string arg6 = "{11223344-5566-7788-99aa-bbccddeeff00}";
    char *argv[] = {arg0.data(), arg1.data(), arg2.data(), arg3.data(),
                    arg4.data(), arg5.data(), arg6.data()};

    Opts::LaunchOptsManager mgr;
    const auto &opts = mgr.Parse(7, argv);
    QVERIFY(opts.HasElevatedTask());
    QVERIFY(!opts.HasCliCommand());
    QCOMPARE(opts.applyLatencyProfile, std::string{"sbc"});
    QCOMPARE(opts.latencyEndpointGuid, arg4);
    QCOMPARE(opts.latencyContainerId, arg6);
}

void ProServicesTests::CompositeTransportPrefersPrimaryAndFallsBack()
{
    using Core::AirPods::CompositeControlTransport;

    // Case 1: Both available -> prefers primary (No-Test-Mode Radio Service).
    {
        auto primary = std::make_unique<FakeControlTransport>("radio_service", true);
        auto fallback = std::make_unique<FakeControlTransport>("kmdf_l2cap", true);
        auto *primaryPtr = primary.get();
        auto *fallbackPtr = fallback.get();

        CompositeControlTransport composite{std::move(primary), std::move(fallback)};
        QVERIFY(composite.IsAvailable());
        QCOMPARE(composite.BackendName(), std::string{"radio_service"});

        bool notifiedConnected = false;
        composite.SetOnConnectionChanged([&](bool c) { notifiedConnected = c; });
        composite.Connect(0x112233445566ull);

        QVERIFY(notifiedConnected);
        QVERIFY(composite.IsConnected());
        QCOMPARE(primaryPtr->lastAddress, 0x112233445566ull);
        QCOMPARE(fallbackPtr->lastAddress, uint64_t{0});
        const std::vector<uint8_t> frame{0x04, 0x00, 0x04, 0x00};
        QVERIFY(composite.SendPacket(frame));
        QCOMPARE(primaryPtr->sentPackets.size(), size_t{1});
        QVERIFY(fallbackPtr->sentPackets.empty());
    }

    // Case 2: Primary unavailable -> transparently falls back to KMDF driver.
    {
        auto primary = std::make_unique<FakeControlTransport>("radio_service", false);
        auto fallback = std::make_unique<FakeControlTransport>("kmdf_l2cap", true);
        auto *fallbackPtr = fallback.get();

        CompositeControlTransport composite{std::move(primary), std::move(fallback)};
        QVERIFY(composite.IsAvailable());
        QCOMPARE(composite.BackendName(), std::string{"kmdf_l2cap"});
        composite.Connect(0xAABBCCDDEEFFull);
        QVERIFY(composite.IsConnected());
        QCOMPARE(fallbackPtr->lastAddress, 0xAABBCCDDEEFFull);
    }
}

void ProServicesTests::NamedPipeIpcRoundTrips()
{
    Core::Ipc::Server server([](const QByteArray &req) -> QByteArray {
        return QByteArrayLiteral("{\"ok\":true,\"echo\":") + req + QByteArrayLiteral("}");
    });
    QVERIFY(server.Start());

    const auto reply = Core::Ipc::SendCommand(QByteArrayLiteral("{\"cmd\":\"ping\"}"));
    server.Stop();

    QVERIFY(reply.has_value());
    QVERIFY(reply->contains("\"ok\":true"));
    QVERIFY(reply->contains("\"cmd\":\"ping\""));
}

QTEST_MAIN(ProServicesTests)
#include "ProServicesTests.moc"
