// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#include "Source/Gui/Widget/Battery.h"

#include <iterator>

#include <QPainter>
#include <QtMath>
#include <QPainterPath>

namespace Gui::Widget {

Battery::Battery(QWidget *parent) : QWidget{parent}
{
    // Add bold
    //
    auto currFont = font();
    currFont.setBold(true);
    setFont(currFont);

    setBatterySize(30, 30);
}

void Battery::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QFontMetrics fontMetrics{this->fontMetrics()};

    //
    // The indicator is a ring, which is how iOS shows the charge of an accessory: a track
    // for the whole circle and an arc over it for the level, sweeping clockwise from the
    // top. Everything is proportional to the diameter so the shape survives a resize.
    //
    const qreal diameter = qMin(_batterySize.width(), _batterySize.height());
    const qreal stroke = _borderWidth > 0 ? _borderWidth : qMax(2.0, diameter * 0.09);

    // A stroke straddles its path, so the ring is inset by half of it to stay inside.
    _batteryRect = QRectF{
        (width() - diameter) / 2.0 + stroke / 2.0, stroke / 2.0, diameter - stroke,
        diameter - stroke};

    // The bolt sits in the hollow of the ring, clear of the arc entirely.
    _chargingRect = _batteryRect;

    if (_isShowText) {
        _textRect =
            QRectF{0.0, diameter + _textPadding, (qreal)width(), (qreal)fontMetrics.height()};
    }

    QPainter painter{this};
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

    drawBorder(painter);
    drawBackground(painter);
    drawChargingIcon(painter);
    drawText(painter);
}

void Battery::drawBorder(QPainter &painter)
{
    // The unfilled track: the whole circle, under the arc.
    painter.save();
    {
        const qreal diameter = qMin(_batterySize.width(), _batterySize.height());
        const qreal stroke = _borderWidth > 0 ? _borderWidth : qMax(2.0, diameter * 0.09);

        painter.setPen(QPen{_borderColor, stroke, Qt::SolidLine, Qt::FlatCap});
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(_batteryRect);
    }
    painter.restore();
}

void Battery::drawBackground(QPainter &painter)
{
    if (_value <= _minValue) {
        return;
    }

    painter.save();
    {
        const qreal diameter = qMin(_batterySize.width(), _batterySize.height());
        const qreal stroke = _borderWidth > 0 ? _borderWidth : qMax(2.0, diameter * 0.09);

        _fillColor = (_isCharging || _value > _alarmValue) ? _normalColor : _alarmColor;

        //
        // Qt measures arcs in sixteenths of a degree, counter-clockwise from three
        // o'clock. Twelve o'clock is therefore 90 degrees, and a clockwise sweep is a
        // negative span.
        //
        constexpr int kTwelveOClock = 90 * 16;
        const auto span =
            static_cast<int>(-360.0 * 16.0 * qMin<qreal>(_value, _maxValue) / _maxValue);

        painter.setPen(QPen{_fillColor, stroke, Qt::SolidLine, Qt::FlatCap});
        painter.setBrush(Qt::NoBrush);
        painter.drawArc(_batteryRect, kTwelveOClock, span);
    }
    painter.restore();
}

void Battery::drawHead(QPainter &painter)
{
    // A ring has no terminal. Kept so the class's shape stays stable for callers.
    Q_UNUSED(painter);
}

void Battery::drawChargingIcon(QPainter &painter)
{
    if (!_isCharging) {
        return;
    }

    painter.save();
    {
        //
        // The bolt, normalised into a unit box and traced clockwise from the top of the
        // upper stroke. It sits in the hollow of the ring, so unlike a bolt drawn over a
        // fill it needs only one colour -- the same green as the arc.
        //
        static constexpr QPointF kBolt[]{
            {0.58, 0.00}, {0.14, 0.56}, {0.42, 0.56}, {0.38, 1.00}, {0.86, 0.42}, {0.56, 0.42},
        };

        const qreal boltHeight = _chargingRect.height() * 0.46;
        const qreal boltWidth = boltHeight * 0.62;
        const QPointF origin{
            _chargingRect.center().x() - boltWidth / 2.0,
            _chargingRect.center().y() - boltHeight / 2.0};

        QPainterPath path;
        for (int i = 0; i < static_cast<int>(std::size(kBolt)); ++i) {
            const QPointF point{
                origin.x() + kBolt[i].x() * boltWidth, origin.y() + kBolt[i].y() * boltHeight};
            if (i == 0) {
                path.moveTo(point);
            }
            else {
                path.lineTo(point);
            }
        }
        path.closeSubpath();

        painter.setPen(Qt::NoPen);
        painter.fillPath(path, QBrush{_fillColor.isValid() ? _fillColor : _normalColor});
    }
    painter.restore();
}

void Battery::drawText(QPainter &painter)
{
    if (!_isShowText) {
        return;
    }

    painter.save();
    {
        QTextOption textOption;
        textOption.setWrapMode(QTextOption::NoWrap);
        textOption.setAlignment(Qt::AlignHCenter);

        painter.setPen(Theme::Colour::TextPrimary);
        painter.drawText(_textRect, QString{"%1%"}.arg(_value), textOption);
    }
    painter.restore();
}

auto Battery::getMinValue() const -> ValueType
{
    return _minValue;
}

auto Battery::getMaxValue() const -> ValueType
{
    return _maxValue;
}

auto Battery::getAlarmValue() const -> ValueType
{
    return _alarmValue;
}

auto Battery::getValue() const -> ValueType
{
    return _value;
}

qreal Battery::getBorderWidth() const
{
    return _borderWidth;
}

qreal Battery::getBorderRadius() const
{
    return _borderRadius;
}

qreal Battery::getBackgroundRadius() const
{
    return _backgroundRadius;
}

qreal Battery::getHeadRadius() const
{
    return _headRadius;
}

QColor Battery::getBorderColor() const
{
    return _borderColor;
}

QColor Battery::getAlarmColor() const
{
    return _alarmColor;
}

QColor Battery::getNormalColor() const
{
    return _normalColor;
}

QColor Battery::getChargingIconColor() const
{
    return _chargingIconColor;
}

bool Battery::isCharging() const
{
    return _isCharging;
}

bool Battery::isShowText() const
{
    return _isShowText;
}

qreal Battery::getTextPadding() const
{
    return _textPadding;
}

// QSize Battery::sizeHint() const
//{
//    return QSize{30, 15};
//}

// QSize Battery::minimumSizeHint() const
//{
//    return QSize{30, 15};
//}

void Battery::setRange(ValueType minValue, ValueType maxValue)
{
    if (minValue >= maxValue) {
        return;
    }

    _minValue = minValue;
    _maxValue = maxValue;

    setValue(_value);
    update();
}

void Battery::setMinValue(ValueType value)
{
    setRange(value, _maxValue);
}

void Battery::setMaxValue(ValueType value)
{
    setRange(_minValue, value);
}

void Battery::setAlarmValue(ValueType value)
{
    if (_alarmValue == value) {
        return;
    }
    _alarmValue = value;
    update();
}

void Battery::setValue(ValueType value)
{
    value = std::clamp(value, _minValue, _maxValue);
    if (_value == value) {
        return;
    }

    _value = value;
    update();

    Q_EMIT valueChanged(_value);
}

void Battery::setBorderWidth(qreal value)
{
    if (_borderWidth == value) {
        return;
    }
    _borderWidth = value;
    update();
}

void Battery::setBorderRadius(qreal value)
{
    if (_borderRadius == value) {
        return;
    }
    _borderRadius = value;
    update();
}

void Battery::setBackgroundRadius(qreal value)
{
    if (_backgroundRadius == value) {
        return;
    }
    _backgroundRadius = value;
    update();
}

void Battery::setHeadRadius(qreal value)
{
    if (_headRadius == value) {
        return;
    }
    _headRadius = value;
    update();
}

void Battery::setBorderColor(const QColor &value)
{
    if (_borderColor == value) {
        return;
    }
    _borderColor = value;
    update();
}

void Battery::setAlarmColor(const QColor &value)
{
    if (_alarmColor == value) {
        return;
    }
    _alarmColor = value;
    update();
}

void Battery::setNormalColor(const QColor &value)
{
    if (_normalColor == value) {
        return;
    }
    _normalColor = value;
    update();
}

void Battery::setChargingIconColor(const QColor &value)
{
    if (_chargingIconColor == value) {
        return;
    }
    _chargingIconColor = value;
    update();
}

void Battery::setCharging(bool value)
{
    if (_isCharging == value) {
        return;
    }
    _isCharging = value;
    update();

    Q_EMIT chargingStateChanged(_isCharging);
}

void Battery::setShowText(bool value)
{
    if (_isShowText == value) {
        return;
    }
    _isShowText = value;
    update();
}

void Battery::setTextPadding(qreal value)
{
    if (_textPadding == value) {
        return;
    }
    _textPadding = value;
    update();
}

void Battery::setBatterySize(int width, int height)
{
    _batterySize = QSizeF{(qreal)width, (qreal)height};

    QFontMetrics fontMetrics{this->fontMetrics()};
    const qreal diameter = qMin(_batterySize.width(), _batterySize.height());
    setFixedSize(
        qCeil(diameter),
        qCeil(diameter + (_isShowText ? (fontMetrics.height() + _textPadding) : 0)));
    update();
}

qreal Battery::getHeadWidth() const
{
    // Proportional to the body height, like everything else in the shape.
    return qMax(1.2, _batterySize.height() * 0.09);
}

qreal Battery::getChargingIconWidth() const
{
    return (_batterySize.height()) / 2.0;
}
} // namespace Gui::Widget
