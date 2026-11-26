// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Service/Protocol/WatchdogPolicy.h"

namespace Service::Protocol {

WatchdogPolicy::WatchdogPolicy(bool initiallyTripped) : _guardTripped{initiallyTripped} {}

void WatchdogPolicy::MarkSessionStarted(int64_t nowMs)
{
    _sessionStartedAtMs = nowMs;
    _sessionActive = true;
}

void WatchdogPolicy::MarkSessionHealthyIfElapsed(int64_t nowMs)
{
    if (_sessionActive && nowMs - _sessionStartedAtMs >= kHealthySessionMs) {
        _failureTimestampsMs.clear();
    }
}

WatchdogPolicy::FailureDecision WatchdogPolicy::RecordTakeoverFailure(int64_t nowMs)
{
    MarkSessionHealthyIfElapsed(nowMs);
    _sessionActive = false;

    Prune(nowMs);
    _failureTimestampsMs.push_back(nowMs);

    const uint32_t count = static_cast<uint32_t>(_failureTimestampsMs.size());
    if (count >= kMaxFailuresInWindow) {
        _guardTripped = true;
        return FailureDecision{
            .action = FailureAction::TripGuardAndRestoreNative,
            .backoffMs = 0,
            .failuresInWindow = count,
        };
    }

    const uint32_t backoffMs = count == 1 ? 1500u : 4000u;
    return FailureDecision{
        .action = FailureAction::RetryWithBackoff,
        .backoffMs = backoffMs,
        .failuresInWindow = count,
    };
}

bool WatchdogPolicy::AllowAdapterReset(int64_t nowMs)
{
    while (!_adapterResetTimestampsMs.empty() &&
           nowMs - _adapterResetTimestampsMs.front() > kAdapterResetWindowMs)
    {
        _adapterResetTimestampsMs.pop_front();
    }
    if (_adapterResetTimestampsMs.size() >= kMaxAdapterResetsInWindow) {
        return false;
    }
    _adapterResetTimestampsMs.push_back(nowMs);
    return true;
}

void WatchdogPolicy::ClearGuard()
{
    _guardTripped = false;
    _failureTimestampsMs.clear();
    _sessionActive = false;
}

bool WatchdogPolicy::IsGuardTripped() const noexcept
{
    return _guardTripped;
}

uint32_t WatchdogPolicy::RecentFailureCount(int64_t nowMs) const
{
    uint32_t count = 0;
    for (int64_t ts : _failureTimestampsMs) {
        if (nowMs - ts <= kFailureWindowMs) {
            ++count;
        }
    }
    return count;
}

uint32_t WatchdogPolicy::RecentAdapterResetCount(int64_t nowMs) const
{
    uint32_t count = 0;
    for (int64_t ts : _adapterResetTimestampsMs) {
        if (nowMs - ts <= kAdapterResetWindowMs) {
            ++count;
        }
    }
    return count;
}

void WatchdogPolicy::Prune(int64_t nowMs)
{
    while (!_failureTimestampsMs.empty() && nowMs - _failureTimestampsMs.front() > kFailureWindowMs)
    {
        _failureTimestampsMs.pop_front();
    }
}

} // namespace Service::Protocol
