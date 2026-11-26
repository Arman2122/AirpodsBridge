// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include <QElapsedTimer>
#include <QString>
#include <QVariantAnimation>
#include <QWidget>

#include "Source/Domain/Base.h"
#include "Source/Presentation/MainWindowPresentation.h"

namespace Gui::Widget {

//
// Apple Health–inspired live Heart Rate telemetry card for optical HR-capable models
// (AirPods Pro 3 and Powerbeats Pro 2).
//
// Displays the latest optical heart-rate reading (tabular BPM), semantic heart-rate zone pill,
// optical lock quality, session Min / Avg / Max range, live status badge, a lub-dub heart
// animation synced to the live BPM on `Gui::FrameClock` (zero idle CPU when hidden or idle),
// and a smooth crimson cubic-Bezier sparkline with vertical gradient fill and Y-axis guides.
// Reads `Theme::Colours()` on every paint so Light and Dark modes stay native.
//
class HeartRateCard : public QWidget
{
    Q_OBJECT

public:
    struct Telemetry {
        bool supported{false};
        bool sensorEnabled{false};
        bool connected{false};
        QString deviceName;
        Core::AirPods::HeartRateStatus status{Core::AirPods::HeartRateStatus::Off};
        std::optional<uint16_t> bpm;
        std::optional<uint8_t> confidence;
        std::optional<uint16_t> minBpm;
        std::optional<uint16_t> maxBpm;
        std::optional<uint16_t> avgBpm;
        std::vector<uint16_t> history;
    };

    explicit HeartRateCard(QWidget *parent = nullptr);

    void SetTelemetry(const Telemetry &telemetry);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void SyncPulseAnimation();

    Telemetry _telemetry;
    QVariantAnimation _pulseAnim{this};
    QElapsedTimer _lastPaintTimer;
    qreal _pulsePhase{0.0};
};

//
// Compact Apple Health live Heart Rate pill bar embedded in the battery popup for AirPods Pro 3
// and Powerbeats Pro 2. Shows pulsing heart, live BPM + Zone pill (or sensor status), a mini
// crimson sparkline, and a one-click Measure / Stop pill button. Clicking the bar body opens
// Settings -> Health.
//
class PopupHeartRateBar : public QWidget
{
    Q_OBJECT

public:
    explicit PopupHeartRateBar(QWidget *parent = nullptr);

    void ApplyPresentation(const HeartRateBarPresentation &presentation);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void ToggleRequested(bool enabled);
    void OpenHealthRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QRectF ActionPillRect() const;
    void SyncPulseAnimation();

    HeartRateBarPresentation _presentation;
    QVariantAnimation _pulseAnim{this};
    QElapsedTimer _lastPaintTimer;
    qreal _pulsePhase{0.0};
    bool _hoverBody{false};
    bool _hoverButton{false};
    bool _pressBody{false};
    bool _pressButton{false};
};

} // namespace Gui::Widget
