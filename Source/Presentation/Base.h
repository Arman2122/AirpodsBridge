// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <QObject>
#include <QString>

#include "Source/Support/Assert.h"

namespace Gui {

enum class Status { Updating, Available, Unavailable, Disconnected, Bind, Unbind };

inline QString DisplayableStatus(const Gui::Status &value)
{
    switch (value) {
    case Gui::Status::Updating:
    case Gui::Status::Available:
    case Gui::Status::Bind:
        APB_ASSERT(false);
    case Gui::Status::Unavailable:
        return QObject::tr("Unavailable");
    case Gui::Status::Disconnected:
        return QObject::tr("Disconnected");
    case Gui::Status::Unbind:
        return QObject::tr("Waiting for Binding");
    default:
        return "Unknown";
    }
}

} // namespace Gui
