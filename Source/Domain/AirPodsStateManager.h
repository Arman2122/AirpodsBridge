// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>

#include "Source/Domain/AapProtocol.h"
#include "Source/Domain/AirPodsAdvertisement.h"
#include "Source/Domain/BatteryTracker.h"
#include "Source/Support/Helper.h"

namespace Core::AirPods::Details {

//
// Reconstructs a stable `Core::AirPods::State` from BLE advertisements (which rotate
// random non-resolvable addresses) and optional two-way AAP control messages.
//
// Owns zero background threads or timers: timestamps are tracked purely in memory, and
// the owner (`Core::AirPods::Manager`) asks `NextExpiry()` to arm a single lazy timer
// only while live state exists, calling `ExpireStale()` when that deadline arrives.
//
class StateManager
{
public:
    using Clock = std::chrono::steady_clock;
    using Timestamp = Clock::time_point;

    static constexpr std::chrono::seconds kStateTimeout{10};
    // BLE proximity advertisements quantise battery in 10% steps; tolerate a one-step change
    // across a random-address rotation without dropping the device.
    static constexpr Battery::ValueType kMaxAddressRotationBatteryDiff{10};
    static constexpr int16_t kMaxRssiDiff{50};

    struct UpdateEvent {
        std::optional<State> oldState;
        State newState;
    };

    StateManager() = default;
    ~StateManager() = default;

    void SetOnDiscardState(std::function<void()> callback);
    std::optional<State> GetCurrentState() const;

    std::optional<UpdateEvent> OnAdvReceived(Advertisement adv, Timestamp now = Clock::now());
    std::optional<UpdateEvent> OnAapMessage(
        const Aap::InboundMessage &message, Model fallbackModel = Model::Unknown,
        Timestamp now = Clock::now());
    std::optional<UpdateEvent>
    SetAapConnected(bool connected, Model fallbackModel = Model::Unknown);
    std::optional<UpdateEvent> SetAudioProfile(AudioProfile profile, QString sourceProcess = {});

    // Returns how long until the next cached side or overall BLE state expires, or
    // `std::nullopt` when idle or when an AAP control transport owns liveness.
    std::optional<Clock::duration> NextExpiry(Timestamp now = Clock::now()) const;

    // Expires any stale side slots and discards BLE state past `kStateTimeout`. Invokes
    // `_onDiscardState` outside the lock if state was discarded.
    bool ExpireStale(Timestamp now = Clock::now());

    void Disconnect();

    void OnRssiMinChanged(int16_t rssiMin);

private:
    mutable std::mutex _mutex;

    std::function<void()> _onDiscardState;
    Helper::Sides<std::optional<std::pair<Advertisement, Timestamp>>> _adv;
    std::optional<Timestamp> _lastBleTimestamp;
    std::optional<State> _cachedState;
    AudioProfile _audioProfile{AudioProfile::Unknown};
    QString _audioProfileSource;
    BatteryTracker _batteryTracker;
    bool _aapConnected{false};
    int16_t _rssiMin{std::numeric_limits<int16_t>::max()};

    bool IsPossibleDesiredAdv(const Advertisement &adv) const;
    void UpdateAdv(Advertisement adv, Timestamp now);
    std::optional<UpdateEvent> UpdateState(Timestamp now);
    std::function<void()> ResetAll();
};

} // namespace Core::AirPods::Details
