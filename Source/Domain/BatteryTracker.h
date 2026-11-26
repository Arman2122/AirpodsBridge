// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <chrono>
#include <optional>

#include <QString>

#include "Source/Domain/AirPodsState.h"

namespace Core::AirPods {

//
// Pure in-memory battery trend tracker.
//
// Observes timestamped device state snapshots and derives:
//   - Estimated remaining listening time (when discharging across observed drops)
//   - Estimated time to full charge (when charging across observed rises)
//   - Asymmetric drain / charging-pin warning (sustained large L/R discrepancy)
//
// Zero timers, zero threads, zero disk I/O; takes an explicit clock time so unit tests
// drive multi-hour sessions deterministically in microseconds.
//
class BatteryTracker
{
public:
    using Clock = std::chrono::steady_clock;

    struct Insights {
        std::optional<int> minutesRemaining;
        std::optional<int> minutesToFull;
        bool asymmetricWarning{false};

        bool operator==(const Insights &) const = default;
    };

    static constexpr uint32_t kAsymmetricGapThreshold = 30;
    static constexpr uint32_t kAsymmetricConsecutiveSamples = 2;
    static constexpr int kMinEstimateMinutes = 5;
    static constexpr int kMaxEstimateMinutes = 48 * 60;

    Insights Observe(const State &state, Clock::time_point now);
    void Reset();

    static QString FormatDuration(int minutes);

private:
    struct ChannelSample {
        uint32_t level{0};
        bool charging{false};
        Clock::time_point timestamp{};
    };

    struct ChannelTracker {
        std::optional<ChannelSample> anchor;
        std::optional<uint32_t> lastLevel;
        std::optional<bool> lastCharging;
        std::optional<int> estimatedMinutes;

        void Update(const Details::BasicState &part, Clock::time_point now);
        void Reset();
    };

    ChannelTracker _left;
    ChannelTracker _right;
    ChannelTracker _device;
    uint32_t _asymmetricStreak{0};
};

} // namespace Core::AirPods
