// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <optional>
#include <vector>

#include <QSize>
#include <QString>

#include "Source/Domain/AirPodsState.h"
#include "Source/Presentation/Base.h"
#include "Source/Presentation/DeviceArtwork.h"

namespace Gui {

enum class ButtonAction : uint32_t {
    NoButton,
    Bind,
    Connect,
};

struct BatteryPresentation {
    bool visible{false};
    bool charging{false};
    uint32_t value{0};

    bool operator==(const BatteryPresentation &) const = default;
};

struct NoiseControlPresentation {
    bool visible{false};
    Core::AirPods::NoiseControlMode activeMode{Core::AirPods::NoiseControlMode::Off};
    bool supportsAdaptive{false};
    uint8_t adaptiveNoiseLevel{50};
    bool allowOffListeningMode{false};
    bool supportsConversationalAwareness{false};
    bool conversationalAwareness{false};
    bool conversationalAwarenessSpeaking{false};
    bool supportsPersonalizedVolume{false};
    bool personalizedVolume{false};
    bool supportsHearingAid{false};
    bool hearingAidEnabled{false};
    std::optional<uint16_t> heartRateBpm;

    bool operator==(const NoiseControlPresentation &) const = default;
};

struct HeartRateBarPresentation {
    bool visible{false};
    bool sensorEnabled{false};
    Core::AirPods::HeartRateStatus status{Core::AirPods::HeartRateStatus::Off};
    std::optional<uint16_t> bpm;
    std::optional<uint8_t> confidence;
    std::vector<uint16_t> history;

    bool operator==(const HeartRateBarPresentation &) const = default;
};

struct MainWindowPresentation {
    QString title;
    QString statusBadge;
    ButtonAction buttonAction{ButtonAction::NoButton};
    std::optional<DeviceArtwork::Artwork> artwork;
    // A pair of buds shows all three. A one-piece device -- AirPods Max -- has one battery
    // and no case: it is `leftBattery`, and the other two stay hidden.
    BatteryPresentation leftBattery;
    BatteryPresentation rightBattery;
    BatteryPresentation caseBattery;
    NoiseControlPresentation noiseControl;
    HeartRateBarPresentation heartRate;
    bool isNearby{false};
    bool connecting{false};

    bool operator==(const MainWindowPresentation &) const = default;
};

class MainWindowViewModel
{
public:
    void UpdateState(const Core::AirPods::State &state);
    void UpdateNearbyState(const Core::AirPods::State &state);
    void ClearNearby();
    void SetBoundDevice(Core::AirPods::Model model, const QString &displayName);
    void Available();
    void Unavailable();
    void Disconnect();
    void Bind();
    void Unbind();
    void SetQuickConnectAvailable(bool available);
    void SetShowHeartRateInPopup(bool show);
    void SetConnecting(bool connecting);
    void SetTransientStatus(const QString &status);

    MainWindowPresentation Present() const;

private:
    Status _status{Status::Unavailable};
    bool _quickConnectAvailable{false};
    bool _showHeartRateInPopup{true};
    bool _connecting{false};
    QString _transientStatus;
    Core::AirPods::Model _boundModel{Core::AirPods::Model::Unknown};
    QString _boundDisplayName;
    std::optional<Core::AirPods::State> _state;
    std::optional<Core::AirPods::State> _nearbyState;
};

} // namespace Gui
