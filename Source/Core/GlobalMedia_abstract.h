// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

namespace Core::GlobalMedia::Details {

class ControllerAbstract
{
public:
    ControllerAbstract() = default;
    virtual ~ControllerAbstract() = default;

    virtual void Play() = 0;
    virtual void Pause() = 0;
};
} // namespace Core::GlobalMedia::Details
