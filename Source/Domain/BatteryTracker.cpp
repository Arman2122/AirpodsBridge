// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Domain/BatteryTracker.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace Core::AirPods {

namespace {

constexpr auto kMinElapsedForSlope = std::chrono::minutes{2};

std::optional<int> ClampEstimate(double rawMinutes)
{
    if (!std::isfinite(rawMinutes) || rawMinutes <= 0.0) {
        return std::nullopt;
    }
    const int rounded = static_cast<int>(std::lround(rawMinutes));
    if (rounded < BatteryTracker::kMinEstimateMinutes ||
        rounded > BatteryTracker::kMaxEstimateMinutes)
    {
        return std::nullopt;
    }
    return rounded;
}

} // namespace

void BatteryTracker::ChannelTracker::Update(const Details::BasicState &part, Clock::time_point now)
{
    if (!part.battery.Available()) {
        Reset();
        return;
    }

    const uint32_t level = part.battery.Value();
    const bool charging = part.isCharging;

    if (!anchor.has_value() || !lastCharging.has_value() || lastCharging.value() != charging) {
        anchor = ChannelSample{.level = level, .charging = charging, .timestamp = now};
        lastLevel = level;
        lastCharging = charging;
        estimatedMinutes.reset();
        return;
    }

    // Ignore upward noise while discharging or downward noise while charging: restart the
    // anchor so a noisy BLE bucket flip never yields a negative or wildly inflated slope.
    if ((!charging && level > anchor->level) || (charging && level < anchor->level)) {
        anchor = ChannelSample{.level = level, .charging = charging, .timestamp = now};
        lastLevel = level;
        estimatedMinutes.reset();
        return;
    }

    lastLevel = level;

    const auto elapsed = now - anchor->timestamp;
    if (elapsed < kMinElapsedForSlope) {
        return;
    }

    const double elapsedMinutes =
        std::chrono::duration_cast<std::chrono::duration<double, std::ratio<60>>>(elapsed).count();

    if (!charging) {
        const uint32_t dropped = anchor->level - level;
        if (dropped == 0) {
            return;
        }
        const double ratePerMinute = static_cast<double>(dropped) / elapsedMinutes;
        if (ratePerMinute <= 0.0) {
            return;
        }
        estimatedMinutes = ClampEstimate(static_cast<double>(level) / ratePerMinute);
    }
    else {
        const uint32_t gained = level - anchor->level;
        if (gained == 0 || level >= 100) {
            if (level >= 100) {
                estimatedMinutes.reset();
            }
            return;
        }
        const double ratePerMinute = static_cast<double>(gained) / elapsedMinutes;
        if (ratePerMinute <= 0.0) {
            return;
        }
        estimatedMinutes = ClampEstimate(static_cast<double>(100 - level) / ratePerMinute);
    }
}

void BatteryTracker::ChannelTracker::Reset()
{
    anchor.reset();
    lastLevel.reset();
    lastCharging.reset();
    estimatedMinutes.reset();
}

BatteryTracker::Insights BatteryTracker::Observe(const State &state, Clock::time_point now)
{
    Insights insights;

    if (IsOnePiece(state.model)) {
        _left.Reset();
        _right.Reset();
        _asymmetricStreak = 0;

        const auto part = OnePieceState(state.pods);
        _device.Update(part, now);
        if (_device.estimatedMinutes.has_value()) {
            if (part.isCharging) {
                insights.minutesToFull = _device.estimatedMinutes;
            }
            else {
                insights.minutesRemaining = _device.estimatedMinutes;
            }
        }
        return insights;
    }

    _device.Reset();
    _left.Update(state.pods.left, now);
    _right.Update(state.pods.right, now);

    // Limiting active bud determines usable stereo time remaining; when charging, report the
    // longer remaining charge time so both buds reach 100%.
    if (!state.pods.left.isCharging && _left.estimatedMinutes.has_value()) {
        insights.minutesRemaining = _left.estimatedMinutes;
    }
    if (!state.pods.right.isCharging && _right.estimatedMinutes.has_value()) {
        if (!insights.minutesRemaining.has_value() ||
            _right.estimatedMinutes.value() < insights.minutesRemaining.value())
        {
            insights.minutesRemaining = _right.estimatedMinutes;
        }
    }

    if (state.pods.left.isCharging && _left.estimatedMinutes.has_value()) {
        insights.minutesToFull = _left.estimatedMinutes;
    }
    if (state.pods.right.isCharging && _right.estimatedMinutes.has_value()) {
        if (!insights.minutesToFull.has_value() ||
            _right.estimatedMinutes.value() > insights.minutesToFull.value())
        {
            insights.minutesToFull = _right.estimatedMinutes;
        }
    }

    if (state.pods.left.battery.Available() && state.pods.right.battery.Available()) {
        const uint32_t l = state.pods.left.battery.Value();
        const uint32_t r = state.pods.right.battery.Value();
        const uint32_t gap = l > r ? (l - r) : (r - l);
        if (gap >= kAsymmetricGapThreshold) {
            ++_asymmetricStreak;
        }
        else {
            _asymmetricStreak = 0;
        }
    }
    else {
        _asymmetricStreak = 0;
    }

    insights.asymmetricWarning = _asymmetricStreak >= kAsymmetricConsecutiveSamples;
    return insights;
}

void BatteryTracker::Reset()
{
    _left.Reset();
    _right.Reset();
    _device.Reset();
    _asymmetricStreak = 0;
}

QString BatteryTracker::FormatDuration(int minutes)
{
    if (minutes <= 0) {
        return {};
    }
    if (minutes < 60) {
        return QStringLiteral("~%1m").arg(minutes);
    }
    const int hours = minutes / 60;
    const int rem = minutes % 60;
    if (rem == 0) {
        return QStringLiteral("~%1h").arg(hours);
    }
    return QStringLiteral("~%1h %2m").arg(hours).arg(rem);
}

} // namespace Core::AirPods
