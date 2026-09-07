// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Presentation/MainWindowPresentation.h"

namespace Gui {
namespace {

BatteryPresentation PresentBattery(const Core::AirPods::Details::BasicState &state)
{
    if (!state.battery.Available()) {
        return {};
    }

    return {
        .visible = true,
        .charging = state.isCharging,
        .value = state.battery.Value(),
    };
}

} // namespace

void MainWindowViewModel::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _state = state;
}

void MainWindowViewModel::Available()
{
    if (_status == Status::Unavailable) {
        _status = Status::Disconnected;
    }
}

void MainWindowViewModel::Unavailable()
{
    _status = Status::Unavailable;
    _state.reset();
}

void MainWindowViewModel::Disconnect()
{
    if (_status == Status::Unbind) {
        return;
    }

    _status = Status::Disconnected;
    _state.reset();
}

void MainWindowViewModel::Bind()
{
    _status = Status::Disconnected;
    _state.reset();
}

void MainWindowViewModel::Unbind()
{
    _status = Status::Unbind;
    _state.reset();
}

MainWindowPresentation MainWindowViewModel::Present() const
{
    MainWindowPresentation result;

    switch (_status) {
    case Status::Unavailable:
    case Status::Disconnected:
    case Status::Unbind:
        result.title = DisplayableStatus(_status);
        break;
    default:
        break;
    }

    if (_status == Status::Unbind) {
        result.buttonAction = ButtonAction::Bind;
    }

    if (!_state.has_value()) {
        return result;
    }

    result.title = _state->displayName;
    result.artwork = DeviceArtwork::Find(_state->model, _state->colour);
    if (Core::AirPods::IsOnePiece(_state->model)) {
        result.leftBattery = PresentBattery(Core::AirPods::OnePieceState(_state->pods));
        return result;
    }
    result.leftBattery = PresentBattery(_state->pods.left);
    result.rightBattery = PresentBattery(_state->pods.right);
    result.caseBattery = PresentBattery(_state->caseBox);
    return result;
}

} // namespace Gui
