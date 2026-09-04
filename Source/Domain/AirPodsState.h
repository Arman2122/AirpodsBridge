// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

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

    bool operator==(const State &rhs) const = default;
};

namespace Details {

QString ResolveDisplayName(QString deviceName, Model model);
} // namespace Details

} // namespace Core::AirPods
