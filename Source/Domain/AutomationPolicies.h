// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <vector>

#include "Source/Domain/AirPodsState.h"

namespace Core::Automation {

//
// The decisions behind the Features page, with no side effects and no platform calls.
//
// Each policy takes what it needs in and hands a decision back. The effects -- pausing
// media, showing a notification, asking Windows to connect -- happen in
// `Core::Automation::Engine`, which feeds these with device events and carries the answers
// out. Keeping the two apart is what lets every rule here be tested without AirPods in
// range, and it is where a future rule goes: add a policy here, an effect there.
//

enum class MediaAction : uint32_t { None, Pause, Play };

//////////////////////////////////////////////////

struct EarDetectionOptions {
    bool enabled{true};
    // Resume media when the AirPods go back in. Off means the rule only ever pauses.
    bool resumePlayback{true};
    // "Worn" means at least one AirPod is in an ear, rather than both.
    bool singleBud{false};
};

class EarDetectionPolicy
{
public:
    static bool IsWorn(const AirPods::State &state, bool singleBud);

    // The action for the change from `previous` to `current`. The first state of a
    // session decides nothing: there is no transition to react to yet.
    static MediaAction OnStateChanged(
        const std::optional<AirPods::State> &previous, const AirPods::State &current,
        const EarDetectionOptions &options);
};

//////////////////////////////////////////////////

//
// Pause when the device drops off.
//
// Only a real connected -> disconnected transition counts. The device reports itself
// disconnected once at every start, before it has ever connected, and that must not pause
// whatever the user is listening to on the speakers.
//
class DisconnectPolicy
{
public:
    MediaAction OnConnectionChanged(bool connected, bool enabled);

private:
    bool _wasConnected{false};
};

//////////////////////////////////////////////////

enum class BatteryComponent : uint32_t { Left, Right, Case, Device };

struct BatteryAlert {
    BatteryComponent component;
    uint32_t level;

    bool operator==(const BatteryAlert &) const = default;
};

struct BatteryAlertOptions {
    bool enabled{true};
    uint32_t threshold{20};
};

//
// One alert per drop.
//
// A component alerts when it first reads at or below the threshold, and arms again only
// once it is charging or has climbed back above the threshold by a margin. A reading that
// hovers around the line must not alert on every advertisement.
//
class BatteryAlertPolicy
{
public:
    static constexpr uint32_t kRearmMargin = 5;

    std::vector<BatteryAlert>
    OnStateChanged(const AirPods::State &state, const BatteryAlertOptions &options);

    // A new session starts with nothing alerted.
    void Reset();

private:
    std::array<bool, 4> _alerted{};
};

//////////////////////////////////////////////////

//
// Connect when the case opens nearby.
//
// Windows takes several seconds to answer a connect request, and the lid-open
// advertisement repeats the whole time, so attempts are spaced by a cooldown.
//
class AutoConnectPolicy
{
public:
    using Clock = std::chrono::steady_clock;

    static constexpr std::chrono::seconds kCooldown{30};
    static constexpr std::chrono::seconds kPostDisconnectCooldown{12};

    bool OnLidOpenedNearby(bool deviceConnected, bool enabled, Clock::time_point now);
    void OnDeviceDisconnected(bool enabled, Clock::time_point now);
    void Reset();

private:
    std::optional<Clock::time_point> _lastAttempt;
    std::optional<Clock::time_point> _disconnectedUntil;
};

//////////////////////////////////////////////////

//
// One-shot full-charge notification.
//
// Alerts once when the AirPods (both buds when present, or the single unit for one-piece
// headsets) reach 100% while charging. Re-arms only after dropping below `kRearmThreshold`
// so a bud fluctuating between 99% and 100% in the case never spams notifications.
//
class FullChargeAlertPolicy
{
public:
    static constexpr uint32_t kFullLevel = 100;
    static constexpr uint32_t kRearmThreshold = 90;

    bool OnStateChanged(const AirPods::State &state, bool enabled);
    void Reset();

private:
    bool _armed{false};
    bool _alerted{false};
};

//////////////////////////////////////////////////

enum class AudioRouteAction : uint32_t { None, SwitchToAirPods, RestorePrevious };

//
// Smart default playback output routing based on wear state.
//
// Only acts on genuine transitions once enabled: putting AirPods in switches Windows'
// default output to AirPods Stereo; taking them out or disconnecting restores the user's
// previous speakers/interface.
//
class SmartAudioRoutePolicy
{
public:
    AudioRouteAction OnStateChanged(
        const std::optional<AirPods::State> &previous, const AirPods::State &current, bool enabled,
        bool singleBud);
    AudioRouteAction OnDisconnected(bool enabled);
    void Reset();

private:
    bool _routedToAirPods{false};
};

} // namespace Core::Automation
