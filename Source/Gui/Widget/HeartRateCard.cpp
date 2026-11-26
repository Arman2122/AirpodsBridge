// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/HeartRateCard.h"

#include <algorithm>
#include <cmath>

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QWindow>

#include "Source/Core/WindowEffects.h"
#include "Source/Gui/Painting.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

namespace {

constexpr int kCardHeight = 154;
constexpr int kPopupBarHeight = 46;
constexpr qreal kChartRadius = 14.0;
constexpr qreal kPopupBarRadius = 14.0;

QColor AppleHealthRed(bool isDark, int alpha = 255)
{
    return isDark ? QColor{255, 55, 95, alpha} : QColor{255, 45, 85, alpha};
}

QColor AppleAmber(bool isDark, int alpha = 255)
{
    return isDark ? QColor{255, 159, 10, alpha} : QColor{235, 134, 0, alpha};
}

struct ZoneVisual {
    QString label;
    QColor colour;
};

ZoneVisual ZonePresentation(Core::AirPods::HeartRateZone zone, bool isDark)
{
    using Core::AirPods::HeartRateZone;
    switch (zone) {
    case HeartRateZone::Resting:
        return {
            HeartRateCard::tr("Resting"),
            isDark ? QColor{100, 210, 255} : QColor{0, 122, 255},
        };
    case HeartRateZone::Normal:
        return {
            HeartRateCard::tr("Normal"),
            isDark ? QColor{48, 209, 88} : QColor{40, 180, 75},
        };
    case HeartRateZone::Elevated:
        return {
            HeartRateCard::tr("Elevated"),
            AppleAmber(isDark),
        };
    case HeartRateZone::Cardio:
        return {
            HeartRateCard::tr("Cardio"),
            isDark ? QColor{255, 105, 65} : QColor{235, 85, 40},
        };
    case HeartRateZone::Peak:
        return {
            HeartRateCard::tr("Peak"),
            AppleHealthRed(isDark),
        };
    }
    return {HeartRateCard::tr("Normal"), AppleHealthRed(isDark)};
}

// Realistic physiological double-peak ("lub-dub") scale factor over phase [0, 1].
qreal LubDubScale(qreal phase)
{
    const qreal lub = std::exp(-std::pow((phase - 0.14) / 0.065, 2.0));
    const qreal dub = std::exp(-std::pow((phase - 0.31) / 0.055, 2.0));
    return 1.0 + 0.17 * lub + 0.10 * dub;
}

// Crisp Apple SF-Symbol `heart.fill` cubic Bezier vector glyph.
void DrawHeartGlyph(QPainter &painter, const QPointF &center, qreal size, const QColor &fill)
{
    const qreal s = size * 0.5;
    QPainterPath heart;
    heart.moveTo(center.x(), center.y() + s * 0.92);
    heart.cubicTo(
        center.x() - s * 1.18, center.y() + s * 0.20, center.x() - s * 1.16, center.y() - s * 0.72,
        center.x() - s * 0.48, center.y() - s * 0.86);
    heart.cubicTo(
        center.x() - s * 0.14, center.y() - s * 0.93, center.x() - s * 0.02, center.y() - s * 0.62,
        center.x(), center.y() - s * 0.48);
    heart.cubicTo(
        center.x() + s * 0.02, center.y() - s * 0.62, center.x() + s * 0.14, center.y() - s * 0.93,
        center.x() + s * 0.48, center.y() - s * 0.86);
    heart.cubicTo(
        center.x() + s * 1.16, center.y() - s * 0.72, center.x() + s * 1.18, center.y() + s * 0.20,
        center.x(), center.y() + s * 0.92);
    heart.closeSubpath();

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPath(heart);
    painter.restore();
}

} // namespace

HeartRateCard::HeartRateCard(QWidget *parent) : QWidget{parent}
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kCardHeight);

    _pulseAnim.setStartValue(0.0);
    _pulseAnim.setEndValue(1.0);
    _pulseAnim.setLoopCount(-1);
    _pulseAnim.setDuration(850);
    connect(&_pulseAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        _pulsePhase = v.toReal();
        // Cap repaints to ~60 Hz on high-refresh displays (e.g. 240-480 Hz) so the pulsing
        // heart stays butter-smooth without burning CPU repainting the sparkline 300 times/s.
        if (!_lastPaintTimer.isValid() || _lastPaintTimer.elapsed() >= 16) {
            _lastPaintTimer.restart();
            update();
        }
    });
}

void HeartRateCard::SetTelemetry(const Telemetry &telemetry)
{
    _telemetry = telemetry;
    SyncPulseAnimation();
    update();
}

void HeartRateCard::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    SyncPulseAnimation();
}

void HeartRateCard::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (_pulseAnim.state() == QAbstractAnimation::Running) {
        _pulseAnim.stop();
        _pulsePhase = 0.0;
    }
}

void HeartRateCard::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        SyncPulseAnimation();
        update();
    }
}

void HeartRateCard::SyncPulseAnimation()
{
    using Core::AirPods::HeartRateStatus;
    const auto *handle = window() != nullptr ? window()->windowHandle() : nullptr;
    const bool allowMotion =
        handle == nullptr || Core::WindowEffects::AnimationDuration(handle, 100) > 0;
    const bool shouldPulse = isVisible() && isEnabled() && allowMotion &&
                             (_telemetry.status == HeartRateStatus::Live ||
                              _telemetry.status == HeartRateStatus::AcquiringLock);

    if (!shouldPulse) {
        if (_pulseAnim.state() == QAbstractAnimation::Running) {
            _pulseAnim.stop();
            _pulsePhase = 0.0;
        }
        return;
    }

    int targetMs = 1100;
    if (_telemetry.bpm.has_value() && _telemetry.bpm.value() >= 30) {
        targetMs = std::clamp(60000 / static_cast<int>(_telemetry.bpm.value()), 360, 1400);
    }
    if (std::abs(_pulseAnim.duration() - targetMs) > 40) {
        _pulseAnim.setDuration(targetMs);
    }
    if (_pulseAnim.state() != QAbstractAnimation::Running) {
        _pulseAnim.start();
    }
}

QSize HeartRateCard::sizeHint() const
{
    return {440, kCardHeight};
}

QSize HeartRateCard::minimumSizeHint() const
{
    return {300, kCardHeight};
}

void HeartRateCard::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const auto &colours = Theme::Colours();
    const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;
    const bool enabled = isEnabled();
    const qreal globalAlpha = enabled ? 1.0 : 0.48;
    painter.setOpacity(globalAlpha);

    using Core::AirPods::HeartRateStatus;
    const QColor heartRed = AppleHealthRed(isDark);
    const QColor amber = AppleAmber(isDark);
    const QRectF bounds =
        rect().adjusted(Theme::Metric::RowPaddingH, 14, -Theme::Metric::RowPaddingH, -14);

    const qreal leftColWidth = std::clamp(bounds.width() * 0.44, 178.0, 224.0);
    const QRectF leftRect{bounds.left(), bounds.top(), leftColWidth, bounds.height()};
    const QRectF chartRect{
        leftRect.right() + 16.0, bounds.top(),
        std::max(110.0, bounds.right() - (leftRect.right() + 16.0)), bounds.height()};

    // --- Left column: Apple Health Heart Rate header + zone pill ---
    const qreal heartScale = (_telemetry.status == HeartRateStatus::Live &&
                              _pulseAnim.state() == QAbstractAnimation::Running)
                                 ? LubDubScale(_pulsePhase)
                                 : (_telemetry.status == HeartRateStatus::AcquiringLock
                                        ? (1.0 + 0.08 * std::sin(_pulsePhase * 6.2831853))
                                        : 1.0);
    const QPointF heartPos{leftRect.left() + 7.5, leftRect.top() + 9.5};
    DrawHeartGlyph(painter, heartPos, 13.0 * heartScale, heartRed);

    QFont labelFont = Theme::Font::Footnote();
    labelFont.setWeight(QFont::DemiBold);
    painter.setFont(labelFont);
    painter.setPen(heartRed);
    const QString titleText = tr("Heart Rate");
    const qreal titleW = QFontMetricsF{labelFont}.horizontalAdvance(titleText);
    painter.drawText(
        QRectF{leftRect.left() + 19.0, leftRect.top(), titleW + 4.0, 18.0},
        Qt::AlignLeft | Qt::AlignVCenter, titleText);

    // Heart-rate zone pill beside the header when live
    if (_telemetry.bpm.has_value()) {
        const auto zoneVis =
            ZonePresentation(Core::AirPods::HeartRateZoneForBpm(_telemetry.bpm.value()), isDark);
        QFont pillFont = Theme::Font::Footnote();
        pillFont.setPointSizeF(7.8);
        pillFont.setWeight(QFont::DemiBold);
        painter.setFont(pillFont);
        const qreal pillW = QFontMetricsF{pillFont}.horizontalAdvance(zoneVis.label) + 12.0;
        const qreal pillX = leftRect.left() + 19.0 + titleW + 8.0;
        if (pillX + pillW <= leftRect.right()) {
            const QRectF pillRect{pillX, leftRect.top() + 1.5, pillW, 15.0};
            QColor pillBg = zoneVis.colour;
            pillBg.setAlpha(isDark ? 42 : 28);
            painter.setPen(Qt::NoPen);
            painter.setBrush(pillBg);
            painter.drawRoundedRect(pillRect, 7.5, 7.5);
            painter.setPen(zoneVis.colour);
            painter.drawText(pillRect, Qt::AlignCenter, zoneVis.label);
        }
    }

    // Large tabular BPM value + "BPM" unit + optical signal quality
    QFont bpmFont = Theme::Font::Title();
    bpmFont.setPointSize(28);
    bpmFont.setWeight(QFont::Bold);
    bpmFont.setStyleHint(QFont::SansSerif);
    const QString bpmText =
        _telemetry.bpm.has_value() ? QString::number(_telemetry.bpm.value()) : QStringLiteral("--");

    painter.setFont(bpmFont);
    painter.setPen(colours.TextPrimary);
    const QFontMetricsF bpmMetrics{bpmFont};
    const qreal bpmWidth = bpmMetrics.horizontalAdvance(bpmText);
    const QRectF bpmRect{leftRect.left(), leftRect.top() + 28.0, bpmWidth + 8.0, 42.0};
    painter.drawText(bpmRect, Qt::AlignLeft | Qt::AlignVCenter, bpmText);

    QFont unitFont = Theme::Font::Caption();
    unitFont.setWeight(QFont::DemiBold);
    painter.setFont(unitFont);
    painter.setPen(colours.TextSecondary);
    painter.drawText(
        QRectF{bpmRect.right() + 2.0, leftRect.top() + 45.0, 42.0, 18.0},
        Qt::AlignLeft | Qt::AlignVCenter, tr("BPM"));

    // Middle caption: optical lock quality when live, or guidance when active
    QFont metaFont = Theme::Font::Footnote();
    metaFont.setPointSizeF(8.3);
    painter.setFont(metaFont);
    if (_telemetry.bpm.has_value() && _telemetry.confidence.has_value()) {
        const int sigPct =
            std::clamp((static_cast<int>(_telemetry.confidence.value()) * 100 + 127) / 255, 1, 100);
        painter.setPen(colours.TextSecondary);
        painter.drawText(
            QRectF{leftRect.left(), leftRect.top() + 78.0, leftRect.width(), 16.0},
            Qt::AlignLeft | Qt::AlignVCenter,
            tr("Optical PPG %1 Signal %2%").arg(QChar{0x00B7}).arg(sigPct));
    }
    else if (_telemetry.status == HeartRateStatus::AcquiringLock) {
        painter.setPen(amber);
        painter.drawText(
            QRectF{leftRect.left(), leftRect.top() + 78.0, leftRect.width(), 16.0},
            Qt::AlignLeft | Qt::AlignVCenter, tr("PPG optical sensor active"));
    }

    // Bottom status / session Min · Avg · Max caption
    const QString hwName = _telemetry.deviceName.isEmpty() ? tr("AirPods") : _telemetry.deviceName;
    QFont subFont = Theme::Font::Footnote();
    painter.setFont(subFont);
    QString statusLine;
    QColor statusColor = colours.TextSecondary;
    if (_telemetry.minBpm.has_value() && _telemetry.maxBpm.has_value()) {
        if (_telemetry.avgBpm.has_value()) {
            statusLine = tr("Min %1 %2 Avg %3 %4 Max %5")
                             .arg(_telemetry.minBpm.value())
                             .arg(QChar{0x00B7})
                             .arg(_telemetry.avgBpm.value())
                             .arg(QChar{0x00B7})
                             .arg(_telemetry.maxBpm.value());
        }
        else {
            statusLine = tr("Range %1%2%3 BPM")
                             .arg(_telemetry.minBpm.value())
                             .arg(QChar{0x2013})
                             .arg(_telemetry.maxBpm.value());
        }
    }
    else if (_telemetry.connected && _telemetry.supported && _telemetry.sensorEnabled) {
        switch (_telemetry.status) {
        case HeartRateStatus::Starting:
            statusLine = tr("Starting optical sensor%1").arg(QChar{0x2026});
            break;
        case HeartRateStatus::AcquiringLock:
            statusLine = tr("Acquiring pulse lock%1").arg(QChar{0x2026});
            statusColor = amber;
            break;
        case HeartRateStatus::WaitingForEar:
            statusLine = tr("Place %1 in ears").arg(hwName);
            break;
        case HeartRateStatus::NoSignal:
            statusLine = tr("No pulse lock %1 adjust ear fit").arg(QChar{0x2014});
            statusColor = amber;
            break;
        case HeartRateStatus::Live:
        case HeartRateStatus::Off:
            statusLine = tr("Waiting for ear contact%1").arg(QChar{0x2026});
            break;
        }
    }
    else if (_telemetry.connected && _telemetry.supported) {
        statusLine = tr("Sensor ready %1 %2").arg(QChar{0x00B7}, hwName);
    }
    else {
        statusLine = tr("AirPods Pro 3 / Powerbeats Pro 2");
    }
    painter.setPen(statusColor);
    painter.drawText(
        QRectF{leftRect.left(), leftRect.bottom() - 20.0, leftRect.width(), 18.0},
        Qt::AlignLeft | Qt::AlignVCenter, statusLine);

    // --- Right column: Recessed Apple Health sparkline card ---
    const QPainterPath chartBgPath = Painting::ContinuousRoundedRect(chartRect, kChartRadius);
    painter.setPen(Qt::NoPen);
    painter.setBrush(isDark ? QColor{255, 255, 255, 11} : QColor{0, 0, 0, 8});
    painter.drawPath(chartBgPath);

    painter.setPen(QPen{isDark ? QColor{255, 255, 255, 22} : QColor{0, 0, 0, 14}, 1.0});
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(chartBgPath);

    const QRectF plotRect = chartRect.adjusted(12.0, 28.0, -34.0, -14.0);

    // Subtle horizontal guide lines
    QPen gridPen{isDark ? QColor{255, 255, 255, 18} : QColor{0, 0, 0, 14}, 1.0, Qt::DashLine};
    painter.setPen(gridPen);
    painter.drawLine(
        QPointF{plotRect.left(), plotRect.top() + 2.0},
        QPointF{plotRect.right(), plotRect.top() + 2.0});
    painter.drawLine(
        QPointF{plotRect.left(), plotRect.center().y()},
        QPointF{plotRect.right(), plotRect.center().y()});
    painter.drawLine(
        QPointF{plotRect.left(), plotRect.bottom() - 2.0},
        QPointF{plotRect.right(), plotRect.bottom() - 2.0});

    if (!_telemetry.history.empty()) {
        const auto [minIt, maxIt] =
            std::minmax_element(_telemetry.history.begin(), _telemetry.history.end());
        const qreal lo = std::max<qreal>(30.0, static_cast<qreal>(*minIt) - 6.0);
        const qreal hi = std::min<qreal>(220.0, static_cast<qreal>(*maxIt) + 6.0);
        const qreal span = std::max<qreal>(12.0, hi - lo);

        // Compact Y-axis scale ticks on right margin
        QFont axisFont = Theme::Font::Footnote();
        axisFont.setPointSizeF(7.4);
        painter.setFont(axisFont);
        painter.setPen(colours.TextSecondary);
        painter.drawText(
            QRectF{plotRect.right() + 4.0, plotRect.top() - 5.0, 26.0, 12.0},
            Qt::AlignRight | Qt::AlignVCenter, QString::number(static_cast<int>(std::round(hi))));
        painter.drawText(
            QRectF{plotRect.right() + 4.0, plotRect.bottom() - 7.0, 26.0, 12.0},
            Qt::AlignRight | Qt::AlignVCenter, QString::number(static_cast<int>(std::round(lo))));

        std::vector<QPointF> pts;
        if (_telemetry.history.size() == 1) {
            const qreal y = plotRect.center().y();
            pts.emplace_back(plotRect.left(), y);
            pts.emplace_back(plotRect.right(), y);
        }
        else {
            const size_t count = _telemetry.history.size();
            pts.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const qreal tx = static_cast<qreal>(i) / static_cast<qreal>(count - 1);
                const qreal ty = (static_cast<qreal>(_telemetry.history[i]) - lo) / span;
                pts.emplace_back(
                    plotRect.left() + tx * plotRect.width(),
                    plotRect.bottom() - 2.0 - ty * (plotRect.height() - 4.0));
            }
        }

        // Smooth cubic Bezier spline through telemetry points
        QPainterPath linePath;
        linePath.moveTo(pts.front());
        for (size_t i = 1; i < pts.size(); ++i) {
            const QPointF &p0 = pts[i - 1];
            const QPointF &p1 = pts[i];
            const qreal midX = (p0.x() + p1.x()) * 0.5;
            linePath.cubicTo(QPointF{midX, p0.y()}, QPointF{midX, p1.y()}, p1);
        }

        // Gradient fill beneath the curve
        QPainterPath fillPath = linePath;
        fillPath.lineTo(pts.back().x(), plotRect.bottom());
        fillPath.lineTo(pts.front().x(), plotRect.bottom());
        fillPath.closeSubpath();

        QLinearGradient areaGrad{plotRect.topLeft(), plotRect.bottomLeft()};
        areaGrad.setColorAt(0.0, AppleHealthRed(isDark, isDark ? 96 : 64));
        areaGrad.setColorAt(1.0, AppleHealthRed(isDark, 0));

        painter.save();
        painter.setClipPath(chartBgPath);
        painter.setPen(Qt::NoPen);
        painter.setBrush(areaGrad);
        painter.drawPath(fillPath);

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{heartRed, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.drawPath(linePath);

        // Glowing current-sample endpoint marker
        const QPointF &last = pts.back();
        const qreal haloR = 5.5 + 2.2 * (heartScale - 1.0) * 5.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(AppleHealthRed(isDark, 72));
        painter.drawEllipse(last, haloR, haloR);
        painter.setBrush(isDark ? QColor{28, 28, 32} : Qt::white);
        painter.drawEllipse(last, 4.0, 4.0);
        painter.setBrush(heartRed);
        painter.drawEllipse(last, 2.6, 2.6);
        painter.restore();

        // Compact "LIVE" pill at top-left of chart (leaves top-right Y-axis tick unobstructed)
        QFont badgeFont = Theme::Font::Footnote();
        badgeFont.setPointSizeF(7.6);
        badgeFont.setWeight(QFont::DemiBold);
        painter.setFont(badgeFont);
        const QRectF liveRect{chartRect.left() + 10.0, chartRect.top() + 7.0, 38.0, 15.0};
        painter.setPen(Qt::NoPen);
        painter.setBrush(AppleHealthRed(isDark, isDark ? 42 : 26));
        painter.drawRoundedRect(liveRect, 7.5, 7.5);
        painter.setPen(heartRed);
        painter.drawText(liveRect, Qt::AlignCenter, tr("LIVE"));
    }
    else {
        // Resting ECG baseline + gentle PQRST silhouette when idle/waiting
        const QRectF idlePlot = chartRect.adjusted(12.0, 18.0, -12.0, -20.0);
        const qreal cy = idlePlot.center().y() + 4.0;
        const qreal cx = idlePlot.center().x();
        QPainterPath ecg;
        ecg.moveTo(idlePlot.left(), cy);
        ecg.lineTo(cx - 26.0, cy);
        ecg.lineTo(cx - 18.0, cy - 4.0);
        ecg.lineTo(cx - 12.0, cy);
        ecg.lineTo(cx - 7.0, cy + 5.0);
        ecg.lineTo(cx - 1.0, cy - 16.0);
        ecg.lineTo(cx + 5.0, cy + 9.0);
        ecg.lineTo(cx + 11.0, cy);
        ecg.lineTo(cx + 18.0, cy - 5.0);
        ecg.lineTo(cx + 26.0, cy);
        ecg.lineTo(idlePlot.right(), cy);

        const QColor ecgColor = (_telemetry.status == HeartRateStatus::AcquiringLock)
                                    ? AppleAmber(isDark, isDark ? 160 : 140)
                                    : AppleHealthRed(isDark, isDark ? 96 : 74);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{ecgColor, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.drawPath(ecg);

        if (_telemetry.status == HeartRateStatus::AcquiringLock ||
            _telemetry.status == HeartRateStatus::Starting)
        {
            QFont badgeFont = Theme::Font::Footnote();
            badgeFont.setPointSizeF(7.4);
            badgeFont.setWeight(QFont::DemiBold);
            painter.setFont(badgeFont);
            const QRectF lockRect{chartRect.left() + 10.0, chartRect.top() + 7.0, 56.0, 15.0};
            painter.setPen(Qt::NoPen);
            painter.setBrush(AppleAmber(isDark, isDark ? 42 : 28));
            painter.drawRoundedRect(lockRect, 7.5, 7.5);
            painter.setPen(amber);
            painter.drawText(
                lockRect, Qt::AlignCenter,
                _telemetry.status == HeartRateStatus::AcquiringLock ? tr("LOCKING")
                                                                    : tr("STARTING"));
        }

        QFont hintFont = Theme::Font::Footnote();
        hintFont.setPointSizeF(8.0);
        painter.setFont(hintFont);
        painter.setPen(colours.TextSecondary);
        QString hint = tr("Wear %1 to measure").arg(hwName);
        if (_telemetry.status == HeartRateStatus::AcquiringLock) {
            hint = tr("Hold still %1 locking onto pulse%2").arg(QChar{0x00B7}).arg(QChar{0x2026});
        }
        else if (_telemetry.status == HeartRateStatus::Starting) {
            hint = tr("Initializing RTBuddy PPG sensor%1").arg(QChar{0x2026});
        }
        else if (_telemetry.status == HeartRateStatus::NoSignal) {
            hint = tr("Check ear tip seal and re-seat buds");
        }
        painter.drawText(
            QRectF{
                chartRect.left() + 8.0, chartRect.bottom() - 20.0, chartRect.width() - 16.0, 14.0},
            Qt::AlignCenter, hint);
    }
}

//////////////////////////////////////////////////

PopupHeartRateBar::PopupHeartRateBar(QWidget *parent) : QWidget{parent}
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(kPopupBarHeight);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setToolTip(tr("Live Heart Rate — click to open Health settings"));

    _pulseAnim.setStartValue(0.0);
    _pulseAnim.setEndValue(1.0);
    _pulseAnim.setLoopCount(-1);
    _pulseAnim.setDuration(850);
    connect(&_pulseAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        _pulsePhase = v.toReal();
        if (!_lastPaintTimer.isValid() || _lastPaintTimer.elapsed() >= 16) {
            _lastPaintTimer.restart();
            update();
        }
    });
}

void PopupHeartRateBar::ApplyPresentation(const HeartRateBarPresentation &presentation)
{
    if (_presentation == presentation) {
        return;
    }
    _presentation = presentation;
    SyncPulseAnimation();
    update();
}

QSize PopupHeartRateBar::sizeHint() const
{
    return {308, kPopupBarHeight};
}

QSize PopupHeartRateBar::minimumSizeHint() const
{
    return {240, kPopupBarHeight};
}

QRectF PopupHeartRateBar::ActionPillRect() const
{
    constexpr qreal pillW = 62.0;
    constexpr qreal pillH = 26.0;
    return QRectF{
        static_cast<qreal>(width()) - 10.0 - pillW,
        (static_cast<qreal>(height()) - pillH) * 0.5,
        pillW,
        pillH,
    };
}

void PopupHeartRateBar::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    SyncPulseAnimation();
}

void PopupHeartRateBar::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (_pulseAnim.state() == QAbstractAnimation::Running) {
        _pulseAnim.stop();
        _pulsePhase = 0.0;
    }
}

void PopupHeartRateBar::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::EnabledChange) {
        SyncPulseAnimation();
        update();
    }
}

void PopupHeartRateBar::SyncPulseAnimation()
{
    using Core::AirPods::HeartRateStatus;
    const auto *handle = window() != nullptr ? window()->windowHandle() : nullptr;
    const bool allowMotion =
        handle == nullptr || Core::WindowEffects::AnimationDuration(handle, 100) > 0;
    const bool shouldPulse = isVisible() && isEnabled() && allowMotion &&
                             (_presentation.status == HeartRateStatus::Live ||
                              _presentation.status == HeartRateStatus::AcquiringLock);

    if (!shouldPulse) {
        if (_pulseAnim.state() == QAbstractAnimation::Running) {
            _pulseAnim.stop();
            _pulsePhase = 0.0;
        }
        return;
    }

    int targetMs = 1100;
    if (_presentation.bpm.has_value() && _presentation.bpm.value() >= 30) {
        targetMs = std::clamp(60000 / static_cast<int>(_presentation.bpm.value()), 360, 1400);
    }
    if (std::abs(_pulseAnim.duration() - targetMs) > 40) {
        _pulseAnim.setDuration(targetMs);
    }
    if (_pulseAnim.state() != QAbstractAnimation::Running) {
        _pulseAnim.start();
    }
}

void PopupHeartRateBar::mouseMoveEvent(QMouseEvent *event)
{
    const QPointF pos = event->localPos();
    const bool onBtn = ActionPillRect().contains(pos);
    const bool changed = (onBtn != _hoverButton) || (!onBtn != _hoverBody);
    _hoverButton = onBtn;
    _hoverBody = !onBtn;
    if (changed) {
        update();
    }
}

void PopupHeartRateBar::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const bool onBtn = ActionPillRect().contains(event->localPos());
    _pressButton = onBtn;
    _pressBody = !onBtn;
    update();
}

void PopupHeartRateBar::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        return;
    }
    const bool onBtn = ActionPillRect().contains(event->localPos());
    const bool wasBtn = _pressButton;
    const bool wasBody = _pressBody;
    _pressButton = false;
    _pressBody = false;
    update();

    if (wasBtn && onBtn) {
        Q_EMIT ToggleRequested(!_presentation.sensorEnabled);
    }
    else if (wasBody && !onBtn && rect().contains(event->pos())) {
        Q_EMIT OpenHealthRequested();
    }
}

void PopupHeartRateBar::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    _hoverBody = false;
    _hoverButton = false;
    _pressBody = false;
    _pressButton = false;
    update();
}

void PopupHeartRateBar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    const auto &colours = Theme::Colours();
    const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;
    const QColor heartRed = AppleHealthRed(isDark);
    const QColor amber = AppleAmber(isDark);

    // Liquid Glass recessed continuous squircle pill bar
    const QRectF cardRect = QRectF{rect()}.adjusted(0.75, 0.75, -0.75, -0.75);
    const QPainterPath cardPath = Painting::ContinuousRoundedRect(cardRect, kPopupBarRadius);

    QColor bg;
    if (_pressBody) {
        bg = isDark ? QColor{255, 255, 255, 22} : QColor{0, 0, 0, 16};
    }
    else if (_hoverBody) {
        bg = isDark ? QColor{255, 255, 255, 18} : QColor{0, 0, 0, 12};
    }
    else {
        bg = isDark ? QColor{255, 255, 255, 13} : QColor{0, 0, 0, 8};
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(bg);
    painter.drawPath(cardPath);

    QLinearGradient rim{cardRect.topLeft(), cardRect.bottomLeft()};
    if (isDark) {
        rim.setColorAt(0.0, QColor{255, 255, 255, 42});
        rim.setColorAt(1.0, QColor{255, 255, 255, 16});
    }
    else {
        rim.setColorAt(0.0, QColor{255, 255, 255, 180});
        rim.setColorAt(1.0, QColor{0, 0, 0, 18});
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{QBrush{rim}, 1.0});
    painter.drawPath(cardPath);

    // Pulsing crimson heart glyph on the left
    using Core::AirPods::HeartRateStatus;
    const qreal heartScale = (_presentation.status == HeartRateStatus::Live &&
                              _pulseAnim.state() == QAbstractAnimation::Running)
                                 ? LubDubScale(_pulsePhase)
                                 : (_presentation.status == HeartRateStatus::AcquiringLock
                                        ? (1.0 + 0.08 * std::sin(_pulsePhase * 6.2831853))
                                        : 1.0);
    const qreal cy = cardRect.center().y();
    const QPointF heartCenter{cardRect.left() + 20.0, cy};
    // Subtle crimson aura behind the heart when live
    if (_presentation.status == HeartRateStatus::Live) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(AppleHealthRed(isDark, isDark ? 36 : 24));
        painter.drawEllipse(heartCenter, 12.0 * heartScale, 12.0 * heartScale);
    }
    DrawHeartGlyph(painter, heartCenter, 14.0 * heartScale, heartRed);

    const QRectF actionRect = ActionPillRect();
    const qreal textLeft = cardRect.left() + 36.0;
    const bool showSpark = _presentation.history.size() >= 2;
    const qreal sparkWidth = showSpark ? 48.0 : 0.0;
    const qreal sparkRight = actionRect.left() - 10.0;
    const qreal sparkLeft = sparkRight - sparkWidth;
    const qreal textRight = showSpark ? (sparkLeft - 8.0) : (actionRect.left() - 8.0);
    const qreal textAvail = std::max(40.0, textRight - textLeft);

    if (_presentation.bpm.has_value()) {
        // Live BPM readout + "BPM" + semantic Zone pill
        QFont bpmFont = Theme::Font::Title();
        bpmFont.setPointSizeF(14.5);
        bpmFont.setWeight(QFont::Bold);
        painter.setFont(bpmFont);
        painter.setPen(colours.TextPrimary);
        const QString bpmText = QString::number(_presentation.bpm.value());
        const qreal bpmW = QFontMetricsF{bpmFont}.horizontalAdvance(bpmText);
        painter.drawText(
            QRectF{textLeft, cardRect.top(), bpmW + 4.0, cardRect.height()},
            Qt::AlignLeft | Qt::AlignVCenter, bpmText);

        QFont unitFont = Theme::Font::Footnote();
        unitFont.setPointSizeF(8.0);
        unitFont.setWeight(QFont::DemiBold);
        painter.setFont(unitFont);
        painter.setPen(colours.TextSecondary);
        const QString unitText = tr("BPM");
        const qreal unitX = textLeft + bpmW + 4.0;
        const qreal unitW = QFontMetricsF{unitFont}.horizontalAdvance(unitText);
        painter.drawText(
            QRectF{unitX, cardRect.top() + 2.0, unitW + 4.0, cardRect.height()},
            Qt::AlignLeft | Qt::AlignVCenter, unitText);

        const auto zoneVis =
            ZonePresentation(Core::AirPods::HeartRateZoneForBpm(_presentation.bpm.value()), isDark);
        QFont pillFont = Theme::Font::Footnote();
        pillFont.setPointSizeF(7.6);
        pillFont.setWeight(QFont::DemiBold);
        painter.setFont(pillFont);
        const qreal pillW = QFontMetricsF{pillFont}.horizontalAdvance(zoneVis.label) + 12.0;
        const qreal pillX = unitX + unitW + 7.0;
        if (pillX + pillW <= textRight) {
            const QRectF zoneRect{pillX, cy - 8.0, pillW, 16.0};
            QColor zBg = zoneVis.colour;
            zBg.setAlpha(isDark ? 40 : 28);
            painter.setPen(Qt::NoPen);
            painter.setBrush(zBg);
            painter.drawRoundedRect(zoneRect, 8.0, 8.0);
            painter.setPen(zoneVis.colour);
            painter.drawText(zoneRect, Qt::AlignCenter, zoneVis.label);
        }
    }
    else {
        // Compact two-line status when not yet locked or idle
        QFont titleFont = Theme::Font::Caption();
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.setPen(colours.TextPrimary);
        painter.drawText(
            QRectF{textLeft, cardRect.top() + 6.0, textAvail, 16.0},
            Qt::AlignLeft | Qt::AlignVCenter, tr("Heart Rate"));

        QFont subFont = Theme::Font::Footnote();
        subFont.setPointSizeF(7.8);
        painter.setFont(subFont);
        QString sub = tr("Optical PPG ready");
        QColor subCol = colours.TextSecondary;
        if (_presentation.sensorEnabled) {
            switch (_presentation.status) {
            case HeartRateStatus::Starting:
                sub = tr("Starting sensor%1").arg(QChar{0x2026});
                break;
            case HeartRateStatus::AcquiringLock:
                sub = tr("Acquiring pulse%1").arg(QChar{0x2026});
                subCol = amber;
                break;
            case HeartRateStatus::WaitingForEar:
                sub = tr("Put buds in ears");
                break;
            case HeartRateStatus::NoSignal:
                sub = tr("Adjust ear fit");
                subCol = amber;
                break;
            case HeartRateStatus::Live:
            case HeartRateStatus::Off:
                sub = tr("Waiting for pulse%1").arg(QChar{0x2026});
                break;
            }
        }
        painter.setPen(subCol);
        painter.drawText(
            QRectF{textLeft, cardRect.top() + 23.0, textAvail, 15.0},
            Qt::AlignLeft | Qt::AlignVCenter, sub);
    }

    // Mini crimson sparkline when live history is available
    if (showSpark) {
        const QRectF sparkRect{sparkLeft, cy - 9.0, sparkWidth, 18.0};
        const size_t n = std::min<size_t>(_presentation.history.size(), 16);
        const size_t start = _presentation.history.size() - n;
        const auto [minIt, maxIt] = std::minmax_element(
            _presentation.history.begin() + static_cast<std::ptrdiff_t>(start),
            _presentation.history.end());
        const qreal lo = static_cast<qreal>(*minIt) - 3.0;
        const qreal span = std::max<qreal>(8.0, static_cast<qreal>(*maxIt) + 3.0 - lo);

        QPainterPath spark;
        QPointF prev;
        for (size_t i = 0; i < n; ++i) {
            const qreal tx = static_cast<qreal>(i) / static_cast<qreal>(n - 1);
            const qreal ty = (static_cast<qreal>(_presentation.history[start + i]) - lo) / span;
            const QPointF pt{
                sparkRect.left() + tx * sparkRect.width(),
                sparkRect.bottom() - ty * sparkRect.height()};
            if (i == 0) {
                spark.moveTo(pt);
            }
            else {
                const qreal midX = (prev.x() + pt.x()) * 0.5;
                spark.cubicTo(QPointF{midX, prev.y()}, QPointF{midX, pt.y()}, pt);
            }
            prev = pt;
        }
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{heartRed, 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin});
        painter.drawPath(spark);
    }

    // Far-right Measure / Stop pill button
    QColor pillBg;
    QColor pillFg;
    if (_presentation.sensorEnabled) {
        if (_pressButton) {
            pillBg = isDark ? QColor{255, 255, 255, 42} : QColor{0, 0, 0, 28};
        }
        else if (_hoverButton) {
            pillBg = isDark ? QColor{255, 255, 255, 32} : QColor{0, 0, 0, 20};
        }
        else {
            pillBg = isDark ? QColor{255, 255, 255, 22} : QColor{0, 0, 0, 14};
        }
        pillFg = colours.TextPrimary;
    }
    else {
        if (_pressButton) {
            pillBg = AppleHealthRed(isDark, isDark ? 92 : 68);
        }
        else if (_hoverButton) {
            pillBg = AppleHealthRed(isDark, isDark ? 68 : 48);
        }
        else {
            pillBg = AppleHealthRed(isDark, isDark ? 48 : 32);
        }
        pillFg = heartRed;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(pillBg);
    painter.drawRoundedRect(actionRect, actionRect.height() * 0.5, actionRect.height() * 0.5);

    QFont btnFont = Theme::Font::Footnote();
    btnFont.setPointSizeF(8.0);
    btnFont.setWeight(QFont::DemiBold);
    painter.setFont(btnFont);
    painter.setPen(pillFg);
    painter.drawText(
        actionRect, Qt::AlignCenter, _presentation.sensorEnabled ? tr("Stop") : tr("Measure"));
}

} // namespace Gui::Widget
