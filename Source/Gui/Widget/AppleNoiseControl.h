// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <vector>

#include <QVariantAnimation>
#include <QWidget>

#include "Source/Domain/Base.h"
#include "Source/Presentation/MainWindowPresentation.h"

namespace Gui::Widget {

//
// Apple iOS 26 / iOS 27 Liquid Glass AirPods Control Center widget.
//
// Top stadium capsule:
//   - Full stadium pill (`radius = height / 2`) with specular Liquid Glass rim.
//   - On Adaptive-capable AirPods (Pro 2/3, AirPods 4 ANC): exact iOS 27 5-landmark slider
//     (Transparency [dotted halo] -- dotted ring -- Adaptive [dual sparkles] -- solid ring --
//     Noise Cancellation [solid arch]) with an inner nested Adaptive glass track and a
//     luminous Apple-Blue Liquid Glass thumb that supports both click and smooth drag to
//     switch modes or fine-tune Adaptive Audio level.
//   - On 3-mode AirPods (Pro 1, Max): 3-stop Liquid Glass stadium slider (Off -- Transparency
//     -- Noise Cancellation).
//
// Bottom row (FullControlCenter layout):
//   - Perfect circular Liquid Glass Control Center buttons with specular rims and two-line
//     title + status captions (Conversation Awareness with voice waves, Hearing Aid /
//     Personalized Volume, and Listening Off).
//
// Reads `Theme::Colours()` on every paintEvent so Light/Dark transitions update immediately,
// and drives all motion via `QVariantAnimation` on `Gui::FrameClock`.
//
class AppleNoiseControl : public QWidget
{
    Q_OBJECT

public:
    enum class LayoutMode {
        FullControlCenter, // Liquid Glass Listening Mode capsule + circular Control Center row
        CapsuleOnly,       // Compact Liquid Glass Listening Mode capsule only (for Settings)
    };

    explicit AppleNoiseControl(
        LayoutMode layoutMode = LayoutMode::FullControlCenter, QWidget *parent = nullptr);

    void ApplyPresentation(const NoiseControlPresentation &presentation);
    QSize sizeHint() const override;

Q_SIGNALS:
    void NoiseControlModeRequested(Core::AirPods::NoiseControlMode mode);
    void AdaptiveNoiseLevelRequested(uint8_t level);
    void ConversationalAwarenessToggled(bool enabled);
    void PersonalizedVolumeToggled(bool enabled);
    void HearingAidToggled(bool enabled);
    void MoreControlsRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    enum class CircleKind {
        ConversationalAwareness,
        HearingAid,
        PersonalizedVolume,
        ListeningOff,
        MoreControls,
    };

    struct ControlCircle {
        CircleKind kind{CircleKind::ListeningOff};
        QString title;
        QString subtitle;
        bool active{false};
        bool speaking{false};
        QRectF circleRect;
        QRectF hitRect;
    };

    LayoutMode _layoutMode{LayoutMode::FullControlCenter};
    NoiseControlPresentation _presentation;
    std::vector<ControlCircle> _circles;

    // Normalized horizontal thumb position in [0.0, 1.0] across the stadium capsule.
    qreal _thumbPos{0.5};
    bool _thumbVisible{true};
    bool _dragging{false};
    qreal _dragStartX{0.0};
    QVariantAnimation _thumbAnimation;

    int _hoveredCircle{-1};
    int _pressedCircle{-1};
    bool _hoveringCapsule{false};
    bool _keyboardFocus{false};

    QRectF CapsuleRect() const;
    qreal ThumbWidth() const;
    qreal ThumbCenterX(qreal normalizedPos) const;
    qreal NormalizedFromX(qreal x) const;
    qreal TargetPositionForPresentation(const NoiseControlPresentation &p, bool &visible) const;

    void RebuildCircles();
    void LayoutCircles();
    int CircleAt(const QPointF &pos) const;
    void AnimateThumbTo(qreal target, bool animate);
    void CommitSliderPosition(qreal normalizedPos);

    void PaintLiquidGlassCapsule(QPainter &painter) const;
    void PaintControlCircles(QPainter &painter) const;
};

} // namespace Gui::Widget
