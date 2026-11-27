// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/AppleNoiseControl.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QFocusEvent>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QWindow>

#include "Source/Core/WindowEffects.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

namespace {

constexpr qreal kCapsuleHeight = 56.0;
constexpr qreal kCapsuleLabelGap = 7.0;
constexpr qreal kCapsuleLabelHeight = 16.0;
constexpr qreal kCirclesTopGap = 14.0;
constexpr qreal kCircleDiameter = 48.0;
constexpr qreal kCircleTitleGap = 6.0;
constexpr qreal kCircleTitleHeight = 15.0;
constexpr qreal kCircleSubHeight = 14.0;

constexpr int kCapsuleOnlyHeight =
    static_cast<int>(kCapsuleHeight + kCapsuleLabelGap + kCapsuleLabelHeight);
constexpr int kFullControlHeight = static_cast<int>(
    kCapsuleHeight + kCapsuleLabelGap + kCapsuleLabelHeight + kCirclesTopGap + kCircleDiameter +
    kCircleTitleGap + kCircleTitleHeight + kCircleSubHeight + 2.0);

constexpr qreal kThumbInset = 3.0;
constexpr qreal kAdaptiveZoneMinGap = 7.0;
constexpr qreal kTransSplit = 0.125;
constexpr qreal kAncSplit = 0.875;

QColor WithAlpha(QColor c, int alpha)
{
    c.setAlpha(std::clamp(alpha, 0, 255));
    return c;
}

// Apple SF-Symbol `person.fill` silhouette proportions.
void DrawPersonSilhouette(QPainter &painter, const QPointF &center, qreal scale, const QColor &fill)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);

    const qreal headR = 3.65 * scale;
    const QPointF headC{center.x(), center.y() - 2.95 * scale};
    painter.drawEllipse(headC, headR, headR);

    QPainterPath shoulders;
    const qreal halfW = 6.35 * scale;
    const qreal topY = center.y() + 1.55 * scale;
    const qreal bottomY = center.y() + 7.25 * scale;
    shoulders.moveTo(center.x() - halfW, bottomY - 0.8 * scale);
    shoulders.cubicTo(
        center.x() - halfW, topY + 0.9 * scale, center.x() - 2.9 * scale, topY, center.x(), topY);
    shoulders.cubicTo(
        center.x() + 2.9 * scale, topY, center.x() + halfW, topY + 0.9 * scale, center.x() + halfW,
        bottomY - 0.8 * scale);
    shoulders.quadTo(
        center.x() + halfW, bottomY + 0.6 * scale, center.x() + halfW - 1.3 * scale,
        bottomY + 0.6 * scale);
    shoulders.lineTo(center.x() - halfW + 1.3 * scale, bottomY + 0.6 * scale);
    shoulders.quadTo(
        center.x() - halfW, bottomY + 0.6 * scale, center.x() - halfW, bottomY - 0.8 * scale);
    shoulders.closeSubpath();
    painter.drawPath(shoulders);
    painter.restore();
}

// 4-point Apple sparkle star used on the iOS 26/27 Adaptive Audio icon.
void DrawFourPointStar(
    QPainter &painter, const QPointF &center, qreal outerRadius, const QColor &fill)
{
    const qreal inner = outerRadius * 0.30;
    QPainterPath star;
    star.moveTo(center.x(), center.y() - outerRadius);
    star.quadTo(
        center.x() + inner * 0.65, center.y() - inner * 0.65, center.x() + outerRadius, center.y());
    star.quadTo(
        center.x() + inner * 0.65, center.y() + inner * 0.65, center.x(), center.y() + outerRadius);
    star.quadTo(
        center.x() - inner * 0.65, center.y() + inner * 0.65, center.x() - outerRadius, center.y());
    star.quadTo(
        center.x() - inner * 0.65, center.y() - inner * 0.65, center.x(), center.y() - outerRadius);
    star.closeSubpath();

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(fill);
    painter.drawPath(star);
    painter.restore();
}

// Exact iOS 27 Transparency glyph: person silhouette with a dotted halo around the head.
void DrawTransparencyGlyph(
    QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    DrawPersonSilhouette(painter, center + QPointF{0.0, 0.8 * scale}, scale * 0.94, colour);

    const QPointF headC{center.x(), center.y() - 2.0 * scale};
    const qreal ringR = 6.2 * scale;
    const qreal dotR = 0.88 * scale;

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    // 11 dots arching cleanly around the head from -35 deg through top to 215 deg.
    constexpr int kDots = 11;
    for (int i = 0; i < kDots; ++i) {
        const qreal deg = -28.0 + (236.0 * i) / (kDots - 1);
        const qreal rad = deg * std::numbers::pi / 180.0;
        const QPointF pt{headC.x() + std::cos(rad) * ringR, headC.y() - std::sin(rad) * ringR};
        painter.drawEllipse(pt, dotR, dotR);
    }
    painter.restore();
}

// Exact iOS 27 Noise Cancellation glyph: person silhouette with a bold smooth arch over the head.
void DrawNoiseCancellationGlyph(
    QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    DrawPersonSilhouette(painter, center + QPointF{0.0, 0.8 * scale}, scale * 0.94, colour);

    const QPointF headC{center.x(), center.y() - 2.0 * scale};
    const qreal archR = 6.25 * scale;
    const QRectF archRect{headC.x() - archR, headC.y() - archR, archR * 2.0, archR * 2.0};

    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{colour, 1.85 * scale, Qt::SolidLine, Qt::RoundCap});
    // Smooth continuous arch framing the head from -28 deg to +208 deg (236 deg span).
    painter.drawArc(archRect, static_cast<int>(-28 * 16), static_cast<int>(236 * 16));
    painter.restore();
}

// Exact iOS 27 Adaptive glyph: person silhouette with two 4-point sparkles top-right of the head.
void DrawAdaptiveGlyph(QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    DrawPersonSilhouette(
        painter, center + QPointF{-1.1 * scale, 0.8 * scale}, scale * 0.94, colour);
    DrawFourPointStar(painter, center + QPointF{5.8 * scale, -4.4 * scale}, 3.6 * scale, colour);
    DrawFourPointStar(painter, center + QPointF{2.4 * scale, -7.8 * scale}, 2.0 * scale, colour);
}

// Exact iOS 27 dotted circle stop (Adaptive -> more ambient).
void DrawDottedCircleGlyph(
    QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    constexpr int kDots = 16;
    const qreal ringR = 6.9 * scale;
    const qreal dotR = 0.85 * scale;
    for (int i = 0; i < kDots; ++i) {
        const qreal rad = (2.0 * std::numbers::pi * i) / kDots;
        const QPointF pt{center.x() + std::cos(rad) * ringR, center.y() + std::sin(rad) * ringR};
        painter.drawEllipse(pt, dotR, dotR);
    }
    painter.restore();
}

// Exact iOS 27 solid circle stop (Adaptive -> more noise cancellation).
void DrawSolidCircleGlyph(
    QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{colour, 1.95 * scale, Qt::SolidLine, Qt::RoundCap});
    const qreal r = 6.9 * scale;
    painter.drawEllipse(center, r, r);
    painter.restore();
}

// Exact iOS 27 Listening Off glyph: person silhouette inside a subtle neutral ring.
void DrawOffGlyph(QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    DrawPersonSilhouette(painter, center + QPointF{0.0, 0.7 * scale}, scale * 0.86, colour);
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{WithAlpha(colour, 150), 1.45 * scale, Qt::SolidLine, Qt::RoundCap});
    const qreal r = 9.0 * scale;
    painter.drawEllipse(center, r, r);
    painter.restore();
}

// Exact iOS 27 Conversation Awareness glyph (Screenshot 2): person silhouette + 2 speaking waves.
void DrawConversationGlyph(
    QPainter &painter, const QPointF &center, qreal scale, const QColor &colour, bool speaking)
{
    DrawPersonSilhouette(
        painter, center + QPointF{-2.0 * scale, 0.6 * scale}, scale * 0.90, colour);

    const QColor waveCol = speaking ? Theme::Colours().BatteryGood : colour;
    const QPointF waveOrigin{center.x() + 1.4 * scale, center.y() - 2.4 * scale};
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{waveCol, 1.75 * scale, Qt::SolidLine, Qt::RoundCap});

    const qreal r1 = 4.6 * scale;
    painter.drawArc(
        QRectF{waveOrigin.x() - r1, waveOrigin.y() - r1, r1 * 2.0, r1 * 2.0}, -40 * 16, 80 * 16);

    const qreal r2 = 7.6 * scale;
    painter.drawArc(
        QRectF{waveOrigin.x() - r2, waveOrigin.y() - r2, r2 * 2.0, r2 * 2.0}, -44 * 16, 88 * 16);
    painter.restore();
}

void DrawHearingAidGlyph(
    QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    painter.save();
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{colour, 1.85 * scale, Qt::SolidLine, Qt::RoundCap});

    const QRectF outerEar{
        center.x() - 5.4 * scale, center.y() - 6.6 * scale, 8.2 * scale, 12.6 * scale};
    painter.drawArc(outerEar, -58 * 16, 250 * 16);

    const QRectF innerEar{
        center.x() - 3.0 * scale, center.y() - 3.3 * scale, 4.4 * scale, 6.4 * scale};
    painter.drawArc(innerEar, -32 * 16, 215 * 16);

    const QPointF waveC{center.x() + 2.4 * scale, center.y() - 0.5 * scale};
    const qreal r1 = 4.4 * scale;
    painter.drawArc(QRectF{waveC.x() - r1, waveC.y() - r1, r1 * 2.0, r1 * 2.0}, -42 * 16, 84 * 16);
    painter.restore();
}

void DrawPersonalVolGlyph(
    QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    QPainterPath spk;
    spk.moveTo(center.x() - 6.8 * scale, center.y() - 2.6 * scale);
    spk.lineTo(center.x() - 3.2 * scale, center.y() - 2.6 * scale);
    spk.lineTo(center.x() + 0.8 * scale, center.y() - 6.0 * scale);
    spk.lineTo(center.x() + 0.8 * scale, center.y() + 6.0 * scale);
    spk.lineTo(center.x() - 3.2 * scale, center.y() + 2.6 * scale);
    spk.lineTo(center.x() - 6.8 * scale, center.y() + 2.6 * scale);
    spk.closeSubpath();
    painter.drawPath(spk);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{colour, 1.75 * scale, Qt::SolidLine, Qt::RoundCap});
    const QPointF wc{center.x() + 0.4 * scale, center.y()};
    painter.drawArc(
        QRectF{wc.x() - 4.2 * scale, wc.y() - 4.2 * scale, 8.4 * scale, 8.4 * scale}, -42 * 16,
        84 * 16);
    painter.drawArc(
        QRectF{wc.x() - 6.8 * scale, wc.y() - 6.8 * scale, 13.6 * scale, 13.6 * scale}, -44 * 16,
        88 * 16);
    painter.restore();
}

void DrawSlidersGlyph(QPainter &painter, const QPointF &center, qreal scale, const QColor &colour)
{
    painter.save();
    painter.setPen(QPen{colour, 1.8 * scale, Qt::SolidLine, Qt::RoundCap});
    painter.drawLine(
        QPointF{center.x() - 6.5 * scale, center.y() - 3.5 * scale},
        QPointF{center.x() + 6.5 * scale, center.y() - 3.5 * scale});
    painter.drawLine(
        QPointF{center.x() - 6.5 * scale, center.y() + 3.5 * scale},
        QPointF{center.x() + 6.5 * scale, center.y() + 3.5 * scale});
    painter.setPen(Qt::NoPen);
    painter.setBrush(colour);
    painter.drawEllipse(
        QPointF{center.x() - 2.2 * scale, center.y() - 3.5 * scale}, 2.4 * scale, 2.4 * scale);
    painter.drawEllipse(
        QPointF{center.x() + 2.5 * scale, center.y() + 3.5 * scale}, 2.4 * scale, 2.4 * scale);
    painter.restore();
}

QFont CapsuleLabelFont(bool active)
{
    auto f = Theme::Font::Footnote();
    f.setPointSizeF(8.3);
    f.setWeight(active ? QFont::DemiBold : QFont::Medium);
    return f;
}

QFont CircleTitleFont()
{
    auto f = Theme::Font::Footnote();
    f.setPointSizeF(8.2);
    f.setWeight(QFont::DemiBold);
    return f;
}

QFont CircleSubtitleFont()
{
    auto f = Theme::Font::Footnote();
    f.setPointSizeF(7.6);
    f.setWeight(QFont::Normal);
    return f;
}

} // namespace

AppleNoiseControl::AppleNoiseControl(LayoutMode layoutMode, QWidget *parent)
    : QWidget{parent}, _layoutMode{layoutMode}
{
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAccessibleName(tr("Listening mode"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    _thumbAnimation.setEasingCurve(QEasingCurve::OutCubic);
    connect(&_thumbAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        _thumbPos = v.toReal();
        update();
    });

    RebuildCircles();
}

QSize AppleNoiseControl::sizeHint() const
{
    return {308, _layoutMode == LayoutMode::CapsuleOnly ? kCapsuleOnlyHeight : kFullControlHeight};
}

qreal AppleNoiseControl::TargetPositionForPresentation(
    const NoiseControlPresentation &p, bool &visible) const
{
    using Core::AirPods::NoiseControlMode;
    if (p.supportsAdaptive) {
        switch (p.activeMode) {
        case NoiseControlMode::Off:
            visible = false;
            return 0.5;
        case NoiseControlMode::Transparency:
            visible = true;
            return 0.0;
        case NoiseControlMode::Adaptive: {
            visible = true;
            // Map adaptive noise level (0..100) smoothly into the inner Adaptive zone [0.25..0.75]
            // where 0 = dotted ring (more ambient) and 100 = solid ring (more noise cancellation).
            const qreal frac =
                std::clamp(static_cast<qreal>(p.adaptiveNoiseLevel) / 100.0, 0.0, 1.0);
            return 0.25 + frac * 0.50;
        }
        case NoiseControlMode::NoiseCancellation:
            visible = true;
            return 1.0;
        }
        visible = false;
        return 0.5;
    }

    // 3-stop stadium capsule: Off (0.0) -- Transparency (0.5) -- Noise Cancellation (1.0).
    visible = true;
    switch (p.activeMode) {
    case NoiseControlMode::Off:
        return 0.0;
    case NoiseControlMode::Transparency:
        return 0.5;
    case NoiseControlMode::NoiseCancellation:
    case NoiseControlMode::Adaptive:
        return 1.0;
    }
    return 0.0;
}

void AppleNoiseControl::ApplyPresentation(const NoiseControlPresentation &presentation)
{
    const bool firstLayout = (_presentation.supportsAdaptive != presentation.supportsAdaptive);
    _presentation = presentation;
    RebuildCircles();

    if (!_dragging) {
        bool showThumb = true;
        const qreal target = TargetPositionForPresentation(_presentation, showThumb);
        const bool wasVisible = _thumbVisible;
        _thumbVisible = showThumb;
        AnimateThumbTo(target, !firstLayout && wasVisible && showThumb && isVisible());
    }
    update();
}

void AppleNoiseControl::RebuildCircles()
{
    _circles.clear();
    if (_layoutMode == LayoutMode::CapsuleOnly) {
        return;
    }

    if (_presentation.supportsConversationalAwareness) {
        const bool isSpeaking =
            _presentation.conversationalAwareness && _presentation.conversationalAwarenessSpeaking;
        ControlCircle conv;
        conv.kind = CircleKind::ConversationalAwareness;
        conv.title = tr("Conversation");
        conv.subtitle = isSpeaking ? tr("Speaking")
                                   : (_presentation.conversationalAwareness ? tr("On") : tr("Off"));
        conv.active = _presentation.conversationalAwareness;
        conv.speaking = isSpeaking;
        _circles.push_back(std::move(conv));
    }

    if (_presentation.supportsHearingAid) {
        ControlCircle ha;
        ha.kind = CircleKind::HearingAid;
        ha.title = tr("Hearing Aid");
        ha.subtitle = _presentation.hearingAidEnabled ? tr("On") : tr("Off");
        ha.active = _presentation.hearingAidEnabled;
        _circles.push_back(std::move(ha));
    }
    else if (_presentation.supportsPersonalizedVolume) {
        ControlCircle pv;
        pv.kind = CircleKind::PersonalizedVolume;
        pv.title = tr("Personal Vol");
        pv.subtitle = _presentation.personalizedVolume ? tr("On") : tr("Off");
        pv.active = _presentation.personalizedVolume;
        _circles.push_back(std::move(pv));
    }

    if (_presentation.supportsAdaptive) {
        const bool isOff = _presentation.activeMode == Core::AirPods::NoiseControlMode::Off;
        ControlCircle offBtn;
        offBtn.kind = CircleKind::ListeningOff;
        offBtn.title = tr("Listening Off");
        offBtn.subtitle = isOff ? tr("Active") : tr("Off");
        offBtn.active = isOff;
        _circles.push_back(std::move(offBtn));
    }
    else {
        ControlCircle more;
        more.kind = CircleKind::MoreControls;
        more.title = tr("Controls");
        more.subtitle = tr("Settings");
        more.active = false;
        _circles.push_back(std::move(more));
    }

    LayoutCircles();
}

void AppleNoiseControl::LayoutCircles()
{
    if (_circles.empty()) {
        return;
    }
    const qreal topY = kCapsuleHeight + kCapsuleLabelGap + kCapsuleLabelHeight + kCirclesTopGap;
    const qreal totalH = kCircleDiameter + kCircleTitleGap + kCircleTitleHeight + kCircleSubHeight;
    const qreal slotW = static_cast<qreal>(width()) / static_cast<qreal>(_circles.size());

    for (size_t i = 0; i < _circles.size(); ++i) {
        const qreal cx = slotW * (static_cast<qreal>(i) + 0.5);
        _circles[i].circleRect =
            QRectF{cx - kCircleDiameter / 2.0, topY, kCircleDiameter, kCircleDiameter};
        _circles[i].hitRect =
            QRectF{slotW * static_cast<qreal>(i), topY - 4.0, slotW, totalH + 6.0};
    }
}

QRectF AppleNoiseControl::CapsuleRect() const
{
    return QRectF{1.0, 1.0, static_cast<qreal>(width()) - 2.0, kCapsuleHeight - 2.0};
}

qreal AppleNoiseControl::ThumbWidth() const
{
    const auto cap = CapsuleRect();
    const qreal innerW = std::max(0.0, cap.width() - kThumbInset * 2.0);
    const qreal innerH = cap.height() - kThumbInset * 2.0;
    if (_presentation.supportsAdaptive) {
        // The 5 landmarks sit at 0.00, 0.25, 0.50, 0.75, 1.00 with step S = (innerW - thumbW) / 4.
        // The nested Adaptive pill spans [x(0.25) - thumbW/2, x(0.75) + thumbW/2], so the gap
        // between it and the Transparency/ANC blue thumb is G = S - thumbW = (innerW - 5*thumbW)/4.
        // Solving thumbW = (innerW - 4*G) / 5 guarantees a clean separation gap on both sides at
        // every width (popup and Settings alike) while keeping the thumb flush inside the Adaptive
        // pill across the entire 0..100% range.
        const qreal zoneGap = std::max(kAdaptiveZoneMinGap, innerW * 0.025);
        return std::max(innerH, (innerW - 4.0 * zoneGap) / 5.0);
    }
    return std::max(innerH + 16.0, innerW / 3.0);
}

qreal AppleNoiseControl::ThumbCenterX(qreal normalizedPos) const
{
    const auto cap = CapsuleRect();
    const qreal halfThumb = ThumbWidth() / 2.0;
    const qreal minX = cap.left() + kThumbInset + halfThumb;
    const qreal maxX = cap.right() - kThumbInset - halfThumb;
    return minX + std::clamp(normalizedPos, 0.0, 1.0) * std::max(0.0, maxX - minX);
}

qreal AppleNoiseControl::NormalizedFromX(qreal x) const
{
    const auto cap = CapsuleRect();
    const qreal halfThumb = ThumbWidth() / 2.0;
    const qreal minX = cap.left() + kThumbInset + halfThumb;
    const qreal maxX = cap.right() - kThumbInset - halfThumb;
    if (maxX <= minX) {
        return 0.5;
    }
    return std::clamp((x - minX) / (maxX - minX), 0.0, 1.0);
}

int AppleNoiseControl::CircleAt(const QPointF &pos) const
{
    for (int i = 0; i < static_cast<int>(_circles.size()); ++i) {
        if (_circles[static_cast<size_t>(i)].hitRect.contains(pos)) {
            return i;
        }
    }
    return -1;
}

void AppleNoiseControl::AnimateThumbTo(qreal target, bool animate)
{
    target = std::clamp(target, 0.0, 1.0);
    _thumbAnimation.stop();

    const auto *handle = window() != nullptr ? window()->windowHandle() : nullptr;
    const int duration =
        animate && isVisible() && handle != nullptr
            ? Core::WindowEffects::AnimationDuration(handle, Theme::Duration::Toggle)
            : 0;
    if (duration <= 0) {
        _thumbPos = target;
        update();
        return;
    }

    _thumbAnimation.setDuration(duration);
    _thumbAnimation.setStartValue(_thumbPos);
    _thumbAnimation.setEndValue(target);
    _thumbAnimation.start();
}

void AppleNoiseControl::CommitSliderPosition(qreal normalizedPos)
{
    using Core::AirPods::NoiseControlMode;

    if (!_presentation.supportsAdaptive) {
        NoiseControlMode targetMode = NoiseControlMode::Off;
        qreal snap = 0.0;
        if (normalizedPos < 0.25) {
            targetMode = NoiseControlMode::Off;
            snap = 0.0;
        }
        else if (normalizedPos < 0.75) {
            targetMode = NoiseControlMode::Transparency;
            snap = 0.5;
        }
        else {
            targetMode = NoiseControlMode::NoiseCancellation;
            snap = 1.0;
        }
        _thumbVisible = true;
        AnimateThumbTo(snap, true);
        if (targetMode != _presentation.activeMode) {
            Q_EMIT NoiseControlModeRequested(targetMode);
        }
        return;
    }

    // Continuous iOS 27 Adaptive inner pill:
    //   0.00       = Transparency
    //   0.25..0.75 = Adaptive Audio (continuous 0..100% across the entire brighter inner pill)
    //   1.00       = Noise Cancellation
    _thumbVisible = true;
    if (normalizedPos < kTransSplit) {
        AnimateThumbTo(0.0, true);
        if (_presentation.activeMode != NoiseControlMode::Transparency) {
            Q_EMIT NoiseControlModeRequested(NoiseControlMode::Transparency);
        }
    }
    else if (normalizedPos > kAncSplit) {
        AnimateThumbTo(1.0, true);
        if (_presentation.activeMode != NoiseControlMode::NoiseCancellation) {
            Q_EMIT NoiseControlModeRequested(NoiseControlMode::NoiseCancellation);
        }
    }
    else {
        const uint8_t prevLevel = _presentation.adaptiveNoiseLevel;
        const qreal clamped = std::clamp(normalizedPos, 0.25, 0.75);
        const uint8_t adaptiveLevel =
            static_cast<uint8_t>(std::lround(((clamped - 0.25) / 0.50) * 100.0));
        const qreal exactPos = 0.25 + (static_cast<qreal>(adaptiveLevel) / 100.0) * 0.50;
        _presentation.adaptiveNoiseLevel = adaptiveLevel;
        AnimateThumbTo(exactPos, true);
        if (_presentation.activeMode != NoiseControlMode::Adaptive) {
            Q_EMIT NoiseControlModeRequested(NoiseControlMode::Adaptive);
        }
        if (adaptiveLevel != prevLevel) {
            Q_EMIT AdaptiveNoiseLevelRequested(adaptiveLevel);
        }
    }
}

void AppleNoiseControl::mouseMoveEvent(QMouseEvent *event)
{
    if (!isEnabled()) {
        return;
    }
    const QPointF pos = event->position();
    if (_dragging) {
        _thumbVisible = true;
        _thumbAnimation.stop();
        _thumbPos = NormalizedFromX(pos.x());
        update();
        return;
    }

    const QRectF capsuleHit =
        CapsuleRect().adjusted(0.0, 0.0, 0.0, kCapsuleLabelGap + kCapsuleLabelHeight);
    const bool overCap = capsuleHit.contains(pos);
    const int overCircle = CircleAt(pos);
    if (overCap != _hoveringCapsule || overCircle != _hoveredCircle) {
        _hoveringCapsule = overCap;
        _hoveredCircle = overCircle;
        setCursor((overCap || overCircle >= 0) ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
}

void AppleNoiseControl::mousePressEvent(QMouseEvent *event)
{
    if (!isEnabled() || event->button() != Qt::LeftButton) {
        return;
    }
    _keyboardFocus = false;
    const QPointF pos = event->position();
    const QRectF capsuleHit =
        CapsuleRect().adjusted(0.0, 0.0, 0.0, kCapsuleLabelGap + kCapsuleLabelHeight);
    if (capsuleHit.contains(pos)) {
        _dragging = true;
        _dragStartX = pos.x();
        _thumbVisible = true;
        _thumbAnimation.stop();
        _thumbPos = NormalizedFromX(pos.x());
        update();
        return;
    }

    _pressedCircle = CircleAt(pos);
    if (_pressedCircle >= 0) {
        update();
    }
}

void AppleNoiseControl::mouseReleaseEvent(QMouseEvent *event)
{
    if (!isEnabled() || event->button() != Qt::LeftButton) {
        return;
    }
    _keyboardFocus = false;
    const QPointF pos = event->position();

    if (_dragging) {
        _dragging = false;
        CommitSliderPosition(NormalizedFromX(pos.x()));
        update();
        return;
    }

    const int releasedCircle = CircleAt(pos);
    const int clicked =
        (_pressedCircle >= 0 && _pressedCircle == releasedCircle) ? releasedCircle : -1;
    _pressedCircle = -1;

    if (clicked >= 0 && clicked < static_cast<int>(_circles.size())) {
        const auto &c = _circles.at(static_cast<size_t>(clicked));
        switch (c.kind) {
        case CircleKind::ConversationalAwareness:
            Q_EMIT ConversationalAwarenessToggled(!c.active);
            break;
        case CircleKind::HearingAid:
            Q_EMIT HearingAidToggled(!c.active);
            break;
        case CircleKind::PersonalizedVolume:
            Q_EMIT PersonalizedVolumeToggled(!c.active);
            break;
        case CircleKind::ListeningOff:
            if (c.active) {
                Q_EMIT NoiseControlModeRequested(
                    Core::AirPods::NoiseControlMode::NoiseCancellation);
            }
            else {
                Q_EMIT NoiseControlModeRequested(Core::AirPods::NoiseControlMode::Off);
            }
            break;
        case CircleKind::MoreControls:
            Q_EMIT MoreControlsRequested();
            break;
        }
    }
    update();
}

void AppleNoiseControl::leaveEvent(QEvent *event)
{
    Q_UNUSED(event);
    _hoveringCapsule = false;
    _hoveredCircle = -1;
    if (!_dragging) {
        _pressedCircle = -1;
    }
    update();
}

void AppleNoiseControl::keyPressEvent(QKeyEvent *event)
{
    if (!isEnabled()) {
        QWidget::keyPressEvent(event);
        return;
    }
    using Core::AirPods::NoiseControlMode;
    if (event->key() == Qt::Key_Left) {
        if (_presentation.supportsAdaptive) {
            if (_presentation.activeMode == NoiseControlMode::NoiseCancellation) {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::Adaptive);
            }
            else if (
                _presentation.activeMode == NoiseControlMode::Adaptive &&
                _presentation.adaptiveNoiseLevel > 0)
            {
                const uint8_t nextLevel =
                    _presentation.adaptiveNoiseLevel > 10
                        ? static_cast<uint8_t>(_presentation.adaptiveNoiseLevel - 10)
                        : uint8_t{0};
                Q_EMIT AdaptiveNoiseLevelRequested(nextLevel);
            }
            else {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::Transparency);
            }
        }
        else {
            if (_presentation.activeMode == NoiseControlMode::NoiseCancellation) {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::Transparency);
            }
            else {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::Off);
            }
        }
        return;
    }
    if (event->key() == Qt::Key_Right) {
        if (_presentation.supportsAdaptive) {
            if (_presentation.activeMode == NoiseControlMode::Transparency ||
                _presentation.activeMode == NoiseControlMode::Off)
            {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::Adaptive);
            }
            else if (
                _presentation.activeMode == NoiseControlMode::Adaptive &&
                _presentation.adaptiveNoiseLevel < 100)
            {
                const uint8_t nextLevel = static_cast<uint8_t>(
                    std::min<int>(100, static_cast<int>(_presentation.adaptiveNoiseLevel) + 10));
                Q_EMIT AdaptiveNoiseLevelRequested(nextLevel);
            }
            else {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::NoiseCancellation);
            }
        }
        else {
            if (_presentation.activeMode == NoiseControlMode::Off) {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::Transparency);
            }
            else {
                Q_EMIT NoiseControlModeRequested(NoiseControlMode::NoiseCancellation);
            }
        }
        return;
    }
    QWidget::keyPressEvent(event);
}

void AppleNoiseControl::focusInEvent(QFocusEvent *event)
{
    const auto reason = event->reason();
    _keyboardFocus = (reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason);
    QWidget::focusInEvent(event);
    update();
}

void AppleNoiseControl::focusOutEvent(QFocusEvent *event)
{
    _keyboardFocus = false;
    QWidget::focusOutEvent(event);
    update();
}

void AppleNoiseControl::PaintLiquidGlassCapsule(QPainter &painter) const
{
    const auto &colours = Theme::Colours();
    const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;
    const bool enabled = isEnabled();
    const QRectF cap = CapsuleRect();
    const qreal capR = cap.height() / 2.0;

    // 1. Outer Liquid Glass stadium capsule
    QLinearGradient trackFill{cap.topLeft(), cap.bottomLeft()};
    if (isDark) {
        trackFill.setColorAt(0.0, enabled ? QColor{66, 66, 72, 232} : QColor{50, 50, 54, 180});
        trackFill.setColorAt(1.0, enabled ? QColor{50, 50, 56, 238} : QColor{42, 42, 46, 180});
    }
    else {
        trackFill.setColorAt(
            0.0, enabled ? QColor{228, 229, 234, 245} : QColor{236, 236, 240, 190});
        trackFill.setColorAt(
            1.0, enabled ? QColor{237, 238, 242, 248} : QColor{240, 240, 244, 190});
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(trackFill);
    painter.drawRoundedRect(cap, capR, capR);

    // Specular 1px Liquid Glass rim around outer capsule (exact iOS 26/27 refraction edge)
    QLinearGradient capRim{cap.topLeft(), cap.bottomLeft()};
    if (isDark) {
        capRim.setColorAt(0.0, QColor{255, 255, 255, enabled ? 102 : 42});
        capRim.setColorAt(0.45, QColor{255, 255, 255, 18});
        capRim.setColorAt(1.0, QColor{255, 255, 255, enabled ? 58 : 26});
    }
    else {
        capRim.setColorAt(0.0, QColor{255, 255, 255, 220});
        capRim.setColorAt(0.5, QColor{0, 0, 0, 18});
        capRim.setColorAt(1.0, QColor{0, 0, 0, 26});
    }
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen{QBrush{capRim}, 1.0});
    painter.drawRoundedRect(cap, capR, capR);

    const qreal cy = cap.center().y();
    const qreal iconScale = 1.06;

    // 2. Nested inner Adaptive glass pill + background landmark glyphs
    const QColor inactiveGlyph =
        !enabled ? WithAlpha(colours.Glyph, 140)
                 : (isDark ? QColor{242, 242, 247, 235} : QColor{44, 44, 48, 230});
    const QColor subtleRingGlyph =
        !enabled ? WithAlpha(colours.Glyph, 95)
                 : (isDark ? QColor{215, 215, 224, 165} : QColor{95, 95, 105, 150});

    if (_presentation.supportsAdaptive) {
        const qreal xTrans = ThumbCenterX(0.0);
        const qreal xDot = ThumbCenterX(0.25);
        const qreal xAdapt = ThumbCenterX(0.50);
        const qreal xSolid = ThumbCenterX(0.75);
        const qreal xAnc = ThumbCenterX(1.0);

        // Inner nested Adaptive zone pill matching the exact 0..100% thumb travel [0.25..0.75]
        const qreal innerPad = ThumbWidth() / 2.0;
        const QRectF adaptiveZone{
            xDot - innerPad, cap.top() + kThumbInset, (xSolid - xDot) + innerPad * 2.0,
            cap.height() - kThumbInset * 2.0};
        const qreal zoneR = adaptiveZone.height() / 2.0;
        painter.setPen(Qt::NoPen);
        painter.setBrush(isDark ? QColor{255, 255, 255, 26} : QColor{0, 0, 0, 14});
        painter.drawRoundedRect(adaptiveZone, zoneR, zoneR);

        // Draw landmarks on the track. When the thumb itself sits inside the Adaptive zone,
        // omit the static center Adaptive glyph so sliding between the dotted and solid rings
        // never shows a duplicate icon behind the glass thumb (matching iOS 27).
        const bool thumbHoldsAdaptive =
            _thumbVisible && enabled && _thumbPos >= kTransSplit && _thumbPos <= kAncSplit;
        DrawTransparencyGlyph(painter, QPointF{xTrans, cy}, iconScale, inactiveGlyph);
        DrawDottedCircleGlyph(painter, QPointF{xDot, cy}, iconScale, subtleRingGlyph);
        if (!thumbHoldsAdaptive) {
            DrawAdaptiveGlyph(painter, QPointF{xAdapt, cy}, iconScale, inactiveGlyph);
        }
        DrawSolidCircleGlyph(painter, QPointF{xSolid, cy}, iconScale, subtleRingGlyph);
        DrawNoiseCancellationGlyph(painter, QPointF{xAnc, cy}, iconScale, inactiveGlyph);
    }
    else {
        DrawOffGlyph(painter, QPointF{ThumbCenterX(0.0), cy}, iconScale, inactiveGlyph);
        DrawTransparencyGlyph(painter, QPointF{ThumbCenterX(0.5), cy}, iconScale, inactiveGlyph);
        DrawNoiseCancellationGlyph(
            painter, QPointF{ThumbCenterX(1.0), cy}, iconScale, inactiveGlyph);
    }

    // 3. Luminous Apple-Blue Liquid Glass Thumb (exact iOS 27 Screenshots 3 & 4)
    if (_thumbVisible && enabled) {
        const qreal thumbW = ThumbWidth();
        const qreal thumbH = cap.height() - kThumbInset * 2.0;
        const qreal thumbCx = ThumbCenterX(_thumbPos);
        const QRectF thumbRect{thumbCx - thumbW / 2.0, cap.top() + kThumbInset, thumbW, thumbH};
        const qreal thumbR = thumbH / 2.0;

        // Soft luminous blue glow under thumb
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor{10, 132, 255, isDark ? 68 : 42});
        painter.drawRoundedRect(
            thumbRect.adjusted(-1.5, 0.5, 1.5, 2.5), thumbR + 1.5, thumbR + 1.5);

        const bool neutralOffThumb = (!_presentation.supportsAdaptive && _thumbPos < 0.25);
        if (neutralOffThumb) {
            painter.setBrush(colours.Raised);
            painter.drawRoundedRect(thumbRect, thumbR, thumbR);
        }
        else {
            // Translucent Liquid Glass blue fill (slightly more translucent while dragging so
            // the dotted/solid rings peek subtly through the glass pill, just like Screenshot 4!)
            const int glassAlpha = _dragging ? 222 : 246;
            QLinearGradient thumbFill{thumbRect.topLeft(), thumbRect.bottomLeft()};
            thumbFill.setColorAt(0.0, QColor{52, 182, 255, glassAlpha});
            thumbFill.setColorAt(0.52, QColor{14, 146, 255, glassAlpha});
            thumbFill.setColorAt(1.0, QColor{0, 122, 255, glassAlpha});
            painter.setBrush(thumbFill);
            painter.drawRoundedRect(thumbRect, thumbR, thumbR);

            // Crisp luminous Liquid Glass specular border around the thumb
            QLinearGradient thumbRim{thumbRect.topLeft(), thumbRect.bottomLeft()};
            thumbRim.setColorAt(0.0, QColor{215, 244, 255, 235});
            thumbRim.setColorAt(0.45, QColor{120, 210, 255, 95});
            thumbRim.setColorAt(1.0, QColor{185, 236, 255, 210});
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{QBrush{thumbRim}, 1.25});
            painter.drawRoundedRect(thumbRect.adjusted(0.4, 0.4, -0.4, -0.4), thumbR, thumbR);
        }

        // Active white glyph centered inside the thumb
        const QColor glyphFg = neutralOffThumb ? colours.TextPrimary : colours.OnAccent;
        if (_presentation.supportsAdaptive) {
            if (_thumbPos < kTransSplit) {
                DrawTransparencyGlyph(painter, thumbRect.center(), iconScale, glyphFg);
            }
            else if (_thumbPos > kAncSplit) {
                DrawNoiseCancellationGlyph(painter, thumbRect.center(), iconScale, glyphFg);
            }
            else {
                DrawAdaptiveGlyph(painter, thumbRect.center(), iconScale, glyphFg);
            }
        }
        else {
            if (_thumbPos < 0.25) {
                DrawOffGlyph(painter, thumbRect.center(), iconScale, glyphFg);
            }
            else if (_thumbPos < 0.75) {
                DrawTransparencyGlyph(painter, thumbRect.center(), iconScale, glyphFg);
            }
            else {
                DrawNoiseCancellationGlyph(painter, thumbRect.center(), iconScale, glyphFg);
            }
        }
    }

    // 4. Focus ring (keyboard navigation only, matching :focus-visible)
    if (hasFocus() && _keyboardFocus) {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen{colours.Focus, 2.0});
        painter.drawRoundedRect(cap.adjusted(1.0, 1.0, -1.0, -1.0), capR, capR);
    }

    // 5. Crisp Apple labels directly underneath the capsule (EXACTLY Screenshots 3 & 4)
    const qreal labelY = cap.bottom() + kCapsuleLabelGap;
    using Core::AirPods::NoiseControlMode;
    const auto drawLabel = [&](const QRectF &r, Qt::Alignment align, const QString &text, bool on) {
        painter.setFont(CapsuleLabelFont(on));
        painter.setPen(
            !enabled ? colours.Glyph : (on ? colours.TextPrimary : colours.TextSecondary));
        painter.drawText(r, align | Qt::AlignVCenter, text);
    };

    if (_presentation.supportsAdaptive) {
        const qreal w = static_cast<qreal>(width());
        const bool draggingAdaptive =
            _dragging && _thumbPos >= kTransSplit && _thumbPos <= kAncSplit;
        const bool adaptiveOn =
            draggingAdaptive || _presentation.activeMode == NoiseControlMode::Adaptive;
        QString adaptiveText = tr("Adaptive");
        if (draggingAdaptive) {
            const qreal clamped = std::clamp(_thumbPos, 0.25, 0.75);
            const int livePct = static_cast<int>(std::lround(((clamped - 0.25) / 0.50) * 100.0));
            adaptiveText =
                QStringLiteral("%1 %2 %3%").arg(tr("Adaptive")).arg(QChar{0x00B7}).arg(livePct);
        }
        else if (
            _presentation.activeMode == NoiseControlMode::Adaptive &&
            (_presentation.adaptiveNoiseLevel < 48 || _presentation.adaptiveNoiseLevel > 52))
        {
            adaptiveText = QStringLiteral("%1 %2 %3%")
                               .arg(tr("Adaptive"))
                               .arg(QChar{0x00B7})
                               .arg(_presentation.adaptiveNoiseLevel);
        }
        drawLabel(
            QRectF{4.0, labelY, w * 0.34, kCapsuleLabelHeight}, Qt::AlignLeft, tr("Transparency"),
            (_dragging && _thumbPos < kTransSplit) ||
                (!_dragging && _presentation.activeMode == NoiseControlMode::Transparency));
        drawLabel(
            QRectF{w * 0.28, labelY, w * 0.44, kCapsuleLabelHeight}, Qt::AlignHCenter, adaptiveText,
            adaptiveOn);
        drawLabel(
            QRectF{w * 0.56, labelY, w * 0.44 - 4.0, kCapsuleLabelHeight}, Qt::AlignRight,
            tr("Noise Cancellation"),
            (_dragging && _thumbPos > kAncSplit) ||
                (!_dragging && _presentation.activeMode == NoiseControlMode::NoiseCancellation));
    }
    else {
        const qreal w = static_cast<qreal>(width());
        drawLabel(
            QRectF{6.0, labelY, w * 0.28, kCapsuleLabelHeight}, Qt::AlignLeft, tr("Off"),
            _presentation.activeMode == NoiseControlMode::Off);
        drawLabel(
            QRectF{w * 0.28, labelY, w * 0.44, kCapsuleLabelHeight}, Qt::AlignHCenter,
            tr("Transparency"), _presentation.activeMode == NoiseControlMode::Transparency);
        drawLabel(
            QRectF{w * 0.56, labelY, w * 0.44 - 6.0, kCapsuleLabelHeight}, Qt::AlignRight,
            tr("Noise Cancellation"),
            _presentation.activeMode == NoiseControlMode::NoiseCancellation);
    }
}

void AppleNoiseControl::PaintControlCircles(QPainter &painter) const
{
    if (_layoutMode == LayoutMode::CapsuleOnly || _circles.empty()) {
        return;
    }

    const auto &colours = Theme::Colours();
    const bool isDark = Theme::CurrentMode() == Theme::Mode::Dark;
    const bool enabled = isEnabled();

    for (int i = 0; i < static_cast<int>(_circles.size()); ++i) {
        const auto &c = _circles.at(static_cast<size_t>(i));
        const QRectF cr = c.circleRect;
        const bool hovered = (i == _hoveredCircle) && enabled;
        const bool pressed = (i == _pressedCircle) && enabled;

        if (c.active && enabled) {
            // Soft Apple-Blue ambient glow under active Liquid Glass circle (Screenshot 2)
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor{10, 132, 255, isDark ? 64 : 38});
            painter.drawEllipse(cr.adjusted(-2.0, 0.5, 2.0, 2.5));

            QLinearGradient fill{cr.topLeft(), cr.bottomLeft()};
            fill.setColorAt(0.0, pressed ? QColor{32, 160, 245} : QColor{50, 180, 255});
            fill.setColorAt(1.0, pressed ? QColor{0, 108, 224} : QColor{10, 132, 255});
            painter.setBrush(fill);
            painter.drawEllipse(cr);

            // Liquid Glass specular circle rim
            QLinearGradient rim{cr.topLeft(), cr.bottomRight()};
            rim.setColorAt(0.0, QColor{215, 244, 255, 235});
            rim.setColorAt(0.5, QColor{120, 210, 255, 90});
            rim.setColorAt(1.0, QColor{190, 236, 255, 200});
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{QBrush{rim}, 1.2});
            painter.drawEllipse(cr.adjusted(0.5, 0.5, -0.5, -0.5));
        }
        else {
            // Frosted neutral Liquid Glass circle (EXACT Screenshot 2 inactive grey circle)
            QLinearGradient fill{cr.topLeft(), cr.bottomLeft()};
            if (isDark) {
                const int boost = pressed ? 18 : (hovered ? 12 : 0);
                fill.setColorAt(0.0, QColor{88 + boost, 88 + boost, 94 + boost, 210});
                fill.setColorAt(1.0, QColor{68 + boost, 68 + boost, 74 + boost, 220});
            }
            else {
                const int drop = pressed ? 16 : (hovered ? 8 : 0);
                fill.setColorAt(0.0, QColor{232 - drop, 233 - drop, 238 - drop, 240});
                fill.setColorAt(1.0, QColor{222 - drop, 223 - drop, 228 - drop, 245});
            }
            painter.setPen(Qt::NoPen);
            painter.setBrush(fill);
            painter.drawEllipse(cr);

            QLinearGradient rim{cr.topLeft(), cr.bottomRight()};
            if (isDark) {
                rim.setColorAt(0.0, QColor{255, 255, 255, 130});
                rim.setColorAt(0.48, QColor{255, 255, 255, 26});
                rim.setColorAt(1.0, QColor{255, 255, 255, 76});
            }
            else {
                rim.setColorAt(0.0, QColor{255, 255, 255, 230});
                rim.setColorAt(0.5, QColor{0, 0, 0, 18});
                rim.setColorAt(1.0, QColor{0, 0, 0, 32});
            }
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen{QBrush{rim}, 1.05});
            painter.drawEllipse(cr.adjusted(0.5, 0.5, -0.5, -0.5));
        }

        const QColor glyphColor = !enabled   ? colours.Glyph
                                  : c.active ? colours.OnAccent
                                  : isDark   ? QColor{245, 245, 250}
                                             : colours.TextPrimary;
        const qreal gScale = 1.06;
        switch (c.kind) {
        case CircleKind::ConversationalAwareness:
            DrawConversationGlyph(painter, cr.center(), gScale, glyphColor, c.speaking);
            break;
        case CircleKind::HearingAid:
            DrawHearingAidGlyph(painter, cr.center(), gScale, glyphColor);
            break;
        case CircleKind::PersonalizedVolume:
            DrawPersonalVolGlyph(painter, cr.center(), gScale, glyphColor);
            break;
        case CircleKind::ListeningOff:
            DrawOffGlyph(painter, cr.center(), gScale, glyphColor);
            break;
        case CircleKind::MoreControls:
            DrawSlidersGlyph(painter, cr.center(), gScale, glyphColor);
            break;
        }

        // Two-line Apple Control Center caption (Title + Status subtitle, Screenshot 2)
        const QRectF titleRect{
            c.hitRect.left() + 2.0, cr.bottom() + kCircleTitleGap, c.hitRect.width() - 4.0,
            kCircleTitleHeight};
        painter.setFont(CircleTitleFont());
        painter.setPen(enabled ? colours.TextPrimary : colours.Glyph);
        painter.drawText(titleRect, Qt::AlignHCenter | Qt::AlignVCenter, c.title);

        const QRectF subRect{
            c.hitRect.left() + 2.0, titleRect.bottom(), c.hitRect.width() - 4.0, kCircleSubHeight};
        painter.setFont(CircleSubtitleFont());
        painter.setPen(
            c.speaking ? colours.BatteryGood : (c.active ? colours.Accent : colours.TextSecondary));
        painter.drawText(subRect, Qt::AlignHCenter | Qt::AlignVCenter, c.subtitle);
    }
}

void AppleNoiseControl::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    LayoutCircles();

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    PaintLiquidGlassCapsule(painter);
    PaintControlCircles(painter);
}

} // namespace Gui::Widget
