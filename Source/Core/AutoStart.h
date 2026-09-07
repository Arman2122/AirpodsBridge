// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <memory>

namespace Core::AutoStart {

class Service
{
public:
    virtual ~Service() = default;
    virtual void SetEnabled(bool enabled) = 0;
};

std::unique_ptr<Service> CreateAutoStartService();

} // namespace Core::AutoStart
