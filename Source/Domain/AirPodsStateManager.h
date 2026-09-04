// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <mutex>
#include <optional>
#include <utility>

#include "Source/Domain/AirPodsAdvertisement.h"
#include "Source/Support/Helper.h"

namespace Core::AirPods::Details {

// AirPods use Random Non-resolvable device addresses for privacy reasons. This means we
// can't "Remember" the user's AirPods by any device property. Here we track our desired
// devices in some non-elegant ways, but obviously it is sometimes unreliable.
//
class StateManager
{
public:
    struct UpdateEvent {
        std::optional<State> oldState;
        State newState;
    };

    StateManager();
    ~StateManager();

    void SetOnDiscardState(std::function<void()> callback);
    std::optional<State> GetCurrentState() const;

    std::optional<UpdateEvent> OnAdvReceived(Advertisement adv);
    void Disconnect();

    void OnRssiMinChanged(int16_t rssiMin);

private:
    using Clock = std::chrono::steady_clock;
    using Timestamp = std::chrono::time_point<Clock>;

    mutable std::mutex _mutex;

    std::function<void()> _onDiscardState;
    Helper::Timer _lostTimer;
    Helper::Sides<Helper::Timer> _stateResetTimer;
    Helper::Sides<std::optional<std::pair<Advertisement, Timestamp>>> _adv;
    std::optional<State> _cachedState;
    int16_t _rssiMin{std::numeric_limits<int16_t>::max()};

    bool IsPossibleDesiredAdv(const Advertisement &adv) const;
    void UpdateAdv(Advertisement adv);
    std::optional<UpdateEvent> UpdateState();
    std::function<void()> ResetAll();

    std::function<void()> DoLost();
    void DoStateReset(Side side);
};

} // namespace Core::AirPods::Details
