// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <deque>

namespace Service::Protocol {

//
// Fail-safe watchdog and circuit breaker for Bluetooth adapter takeover.
//
// Guarantees that a misbehaving adapter or USB hub can never trap the machine without
// Bluetooth:
//   - 3 failed takeovers inside a rolling 10-minute window trip the guard and instruct the
//     service to restore the adapter's native Windows driver immediately.
//   - Once a session remains healthy for >= 60 seconds, the failure window resets.
//   - Real USB adapter resets requested by a vendor driver are rate-limited to 3 per 24h so
//     firmware reload loops cannot thrash the USB bus.
//
class WatchdogPolicy
{
public:
    static constexpr int64_t kFailureWindowMs = 10ll * 60ll * 1000ll;
    static constexpr size_t kMaxFailuresInWindow = 3;
    static constexpr int64_t kHealthySessionMs = 60ll * 1000ll;
    static constexpr int64_t kAdapterResetWindowMs = 24ll * 60ll * 60ll * 1000ll;
    static constexpr size_t kMaxAdapterResetsInWindow = 3;

    enum class FailureAction : uint8_t {
        RetryWithBackoff = 0,
        TripGuardAndRestoreNative = 1,
    };

    struct FailureDecision {
        FailureAction action{FailureAction::RetryWithBackoff};
        uint32_t backoffMs{1000};
        uint32_t failuresInWindow{0};
    };

    explicit WatchdogPolicy(bool initiallyTripped = false);

    void MarkSessionStarted(int64_t nowMs);
    void MarkSessionHealthyIfElapsed(int64_t nowMs);
    FailureDecision RecordTakeoverFailure(int64_t nowMs);
    bool AllowAdapterReset(int64_t nowMs);

    void ClearGuard();
    bool IsGuardTripped() const noexcept;
    uint32_t RecentFailureCount(int64_t nowMs) const;
    uint32_t RecentAdapterResetCount(int64_t nowMs) const;

private:
    void Prune(int64_t nowMs);

    bool _guardTripped{false};
    int64_t _sessionStartedAtMs{0};
    bool _sessionActive{false};
    std::deque<int64_t> _failureTimestampsMs;
    std::deque<int64_t> _adapterResetTimestampsMs;
};

} // namespace Service::Protocol
