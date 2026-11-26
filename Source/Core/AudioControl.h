// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <QObject>
#include <QString>

#include "Source/Domain/Base.h"

namespace Core::AudioControl {

struct AudioEndpoint {
    QString id;
    QString name;
    bool isDefault{false};
    bool isBluetoothHfp{false};

    bool operator==(const AudioEndpoint &) const = default;
};

struct LatencyDiagnostics {
    QString codec{QStringLiteral("AAC")};
    bool aacEnabled{true};
    bool enhancementsEnabled{true};
    double enginePeriodMs{0.0};
    double minPeriodMs{0.0};
    bool keepAliveEnabled{false};
    bool keepAliveActive{false};
    bool applyingProfile{false};
    QString stereoEndpointGuid;
    QString containerId;

    bool IsLowLatencyProfile() const
    {
        return !aacEnabled && !enhancementsEnabled;
    }

    bool operator==(const LatencyDiagnostics &) const = default;
};

struct LatencyProfileApplyResult {
    bool succeeded{false};
    bool a2dpRenegotiated{false};
};

// Pure native Win32 C++ latency profile switcher (zero PowerShell, zero cmd.exe, zero external
// scripts; architecture-safe across x86, x64, and ARM64 Windows). Updates BthA2dp
// `BluetoothAacEnable` and MMDevices `Disable_SysFx` in the native 64-bit registry hive,
// updates USB Bluetooth `MSPower_DeviceEnable` via native COM WMI, and cycles the AirPods/Beats
// A2DP Sink PnP node via `cfgmgr32.dll`. Must be called in an elevated (Administrator) process.
LatencyProfileApplyResult ApplyLatencyProfileInProcess(
    bool lowLatencySbc, const QString &endpointGuid = {}, const QString &containerId = {});

//
// Event-driven Windows Core Audio (WASAPI) companion service for AirPods:
//   1. Detects active Hands-Free Profile (HFP) sessions on the bound AirPods container and
//      identifies the owning process (e.g. Discord.exe, chrome.exe), reporting
//      `AudioProfile::HandsFree` vs `AudioProfile::Stereo`.
//   2. Enforces a user-selected preferred desktop microphone ("Stereo Lock") so games and
//      calls do not silently drop AirPods into 16 kHz mono HFP audio.
//   3. Supports in-ear smart playback switching: routes default output to AirPods A2DP
//      Stereo when worn and restores the user's previous speakers/interface when removed.
//   4. Optionally caps sudden loud reconnect volumes via `IAudioEndpointVolume`.
//   5. Optional wear-aware event-driven WASAPI silent keep-alive (eliminates A2DP
//      AVDTP_SUSPEND wake-up lag while worn, with 0% idle CPU when removed or disconnected)
//      plus live codec/APO/engine telemetry and a UAC-consented SBC Gaming profile switcher.
//
// Driven strictly by COM/WASAPI notifications (`IMMNotificationClient`,
// `IAudioSessionNotification`, `IAudioSessionEvents`) with zero background polling.
//
class Service : public QObject
{
    Q_OBJECT

public:
    explicit Service(QObject *parent = nullptr);
    ~Service() override;

    Service(const Service &) = delete;
    Service &operator=(const Service &) = delete;

    void SetTrackedContainerId(const QString &containerId);
    void SetPreferredMicrophoneId(const QString &endpointId);
    void SetVolumeCap(bool enabled, uint32_t maxPercent);
    void SetKeepAliveEnabled(bool enabled);
    void SetAirPodsWorn(bool worn);
    // When enabled (default), switches Windows default playback output to AirPods Stereo on
    // connect and restores the user's previous output device automatically on disconnect.
    void SetAutoSwitchOnConnect(bool enabled);

    // Smoothly ducks and restores Windows playback volume in response to AirPods AAP 0x4B
    // Conversation Awareness level reports (1..2 = speaking, 3..7 = gradual recovery,
    // 8..9 or 0 = restore). Respects manual user volume changes.
    void OnConversationAwarenessLevel(uint8_t level);
    void RestoreConversationDucking();

    // Switches Windows default playback to the bound AirPods A2DP Stereo endpoint,
    // remembering the current non-AirPods output so `RestorePreviousOutput` can put it back.
    bool SwitchOutputToAirPods();
    // Switches immediately if AirPods Stereo is active, or arms an instant switch the
    // moment Windows activates the AirPods A2DP Stereo endpoint after clicking Connect.
    void RequestSwitchOutputToAirPods();
    bool RestorePreviousOutput();
    bool EnforcePreferredMicrophone();
    void ApplyVolumeCapNow();

    // Launches the elevated (UAC `runas`) Windows BthA2dp codec + APO profile switcher.
    // Returns false if one is already running, the user cancelled UAC, or launch failed.
    bool ApplyLatencyProfileElevated(bool lowLatencySbc);
    // Cleanly stops the active WASAPI keep-alive stream prior to a Bluetooth reconnect pulse so
    // the endpoint tears down silently without audio pops or glitches.
    void SuspendKeepAliveTemporarily();
    void RefreshLatencyDiagnostics();

    std::vector<AudioEndpoint> ListCaptureEndpoints() const;
    std::vector<AudioEndpoint> ListRenderEndpoints() const;

    Core::AirPods::AudioProfile CurrentProfile() const;
    QString CurrentHfpProcess() const;
    LatencyDiagnostics GetLatencyDiagnostics() const;

signals:
    void AudioProfileChanged(Core::AirPods::AudioProfile profile, const QString &sourceProcess);
    void EndpointsChanged();
    void LatencyDiagnosticsChanged();
    void LatencyProfileApplied(bool lowLatencySbc, bool a2dpRenegotiated);

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace Core::AudioControl
