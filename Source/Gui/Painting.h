// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cmath>
#include <numbers>

#include <QPainterPath>
#include <QRectF>

namespace Gui::Painting {

//
// A rounded rectangle whose corners are a superellipse rather than a circular arc.
//
// This is the difference people are picking up on when they say a shape looks like it
// belongs on an Apple platform. A circular corner meets the straight edge with a sudden
// jump in curvature, which the eye reads as a slight pinch. A continuous corner ramps the
// curvature in, so the edge flows into the arc.
//
// The corner is the quadrant of |x/r|^n + |y/r|^n = 1. n = 2 is exactly a circle, and
// larger n squares the shape off; n around 5 is the usual approximation of the corner
// Apple draws. The parameter is exposed so a caller can dial it back rather than having
// the constant buried here.
//
// The curve is emitted as a polyline. At the sizes this is used -- corners of a few tens
// of pixels -- 16 segments per corner is already below the resolution of the display once
// antialiasing is applied, and it avoids the guesswork of fitting Beziers to a curve that
// has no exact Bezier form.
//
inline QPainterPath ContinuousRoundedRect(
    const QRectF &rect, qreal radius, qreal exponent = 5.0, int segmentsPerCorner = 16)
{
    const qreal limit = qMin(rect.width(), rect.height()) / 2.0;
    radius = qBound(0.0, radius, limit);

    QPainterPath path;
    if (radius <= 0.0) {
        path.addRect(rect);
        return path;
    }

    const qreal power = 2.0 / exponent;

    // One corner quadrant, as offsets from the corner of the bounding box: t sweeps from
    // the point where the curve leaves one edge to where it meets the other.
    const auto offsets = [&] {
        QVector<QPointF> points;
        points.reserve(segmentsPerCorner + 1);
        for (int i = 0; i <= segmentsPerCorner; ++i) {
            const qreal t = (std::numbers::pi / 2.0) * i / segmentsPerCorner;
            points.append(QPointF{
                radius * (1.0 - std::pow(std::cos(t), power)),
                radius * (1.0 - std::pow(std::sin(t), power)),
            });
        }
        return points;
    }();

    const QPointF topLeft = rect.topLeft();
    const QPointF topRight = rect.topRight();
    const QPointF bottomRight = rect.bottomRight();
    const QPointF bottomLeft = rect.bottomLeft();

    // Each corner is the same quadrant, mapped into place. `offsets` runs from the point
    // where the curve leaves the first edge to where it meets the second, so every corner
    // is walked in the same forward order and the four arcs chain end-to-start.
    path.moveTo(topLeft.x(), topLeft.y() + radius);
    for (const auto &o : offsets) { // (0, r) -> (r, 0)
        path.lineTo(topLeft.x() + o.x(), topLeft.y() + o.y());
    }
    path.lineTo(topRight.x() - radius, topRight.y());
    for (const auto &o : offsets) { // (-r, 0) -> (0, r), relative to the top-right
        path.lineTo(topRight.x() - o.y(), topRight.y() + o.x());
    }
    path.lineTo(bottomRight.x(), bottomRight.y() - radius);
    for (const auto &o : offsets) { // (0, -r) -> (-r, 0), relative to the bottom-right
        path.lineTo(bottomRight.x() - o.x(), bottomRight.y() - o.y());
    }
    path.lineTo(bottomLeft.x() + radius, bottomLeft.y());
    for (const auto &o : offsets) { // (r, 0) -> (0, -r), relative to the bottom-left
        path.lineTo(bottomLeft.x() + o.y(), bottomLeft.y() - o.x());
    }
    path.closeSubpath();
    return path;
}

} // namespace Gui::Painting
