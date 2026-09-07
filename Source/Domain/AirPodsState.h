// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QString>

#include "Source/Domain/Base.h"

namespace Core::AirPods {

//
// The observable state of a device, reconstructed from its advertisements.
//

namespace Details {

struct BasicState {
    Battery battery;
    bool isCharging{false};

    bool operator==(const BasicState &rhs) const = default;
};
} // namespace Details

struct PodState : Details::BasicState {
    bool isInEar{false};

    bool operator==(const PodState &rhs) const = default;
};

struct CaseState : Details::BasicState {
    bool isBothPodsInCase{false};
    bool isLidOpened{false};

    bool operator==(const CaseState &rhs) const = default;
};

struct PodsState {
    PodState left, right;

    bool operator==(const PodsState &rhs) const = default;
};

struct State {
    Model model{Model::Unknown};
    PodsState pods;
    CaseState caseBox;
    QString displayName;
    // The colour code the device broadcasts, Apple's own. The popup's artwork is picked
    // by it.
    uint8_t colour{0};

    bool operator==(const State &rhs) const = default;
};

//
// The one battery of a one-piece device (`IsOnePiece`), read out of the pair's layout.
//
// Which side carries it is the device's business, and a device may mirror it on both. So
// this is whichever side reports a level; when both do, the lower, and charging if either
// side says so.
//
inline Details::BasicState OnePieceState(const PodsState &pods)
{
    const auto &left = pods.left, &right = pods.right;
    if (!left.battery.Available()) {
        return right;
    }
    if (!right.battery.Available()) {
        return left;
    }
    Details::BasicState result = left.battery.Value() <= right.battery.Value() ? left : right;
    result.isCharging = left.isCharging || right.isCharging;
    return result;
}

namespace Details {

QString ResolveDisplayName(QString deviceName, Model model);
} // namespace Details

} // namespace Core::AirPods
