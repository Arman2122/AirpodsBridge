// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <optional>

#include <QSize>
#include <QString>

#include "Source/Presentation/Base.h"
#include "Source/Domain/AirPodsState.h"

namespace Gui {

enum class ButtonAction : uint32_t {
    NoButton,
    Bind,
};

struct BatteryPresentation {
    bool visible{false};
    bool charging{false};
    uint32_t value{0};

    bool operator==(const BatteryPresentation &) const = default;
};

struct MainWindowPresentation {
    QString title;
    ButtonAction buttonAction{ButtonAction::NoButton};
    std::optional<Core::AirPods::Model> animationModel;
    BatteryPresentation leftBattery;
    BatteryPresentation rightBattery;
    BatteryPresentation caseBattery;

    bool operator==(const MainWindowPresentation &) const = default;
};

struct AnimationPresentation {
    QString resource;
    QSize sourceSize;
};

class MainWindowViewModel
{
public:
    void UpdateState(const Core::AirPods::State &state);
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();

    MainWindowPresentation Present() const;

private:
    Status _status{Status::Unavailable};
    std::optional<Core::AirPods::State> _state;
};

AnimationPresentation GetAnimationPresentation(Core::AirPods::Model model);

} // namespace Gui
