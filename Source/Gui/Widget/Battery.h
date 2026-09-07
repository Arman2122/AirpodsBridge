// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

//
// The original code of this widget was taken from another project.
// https://github.com/feiyangqingyun/QWidgetDemo
//
// And a lot of modifications were made to fit the needs of the project
//
// The author of the original project has licensed "Feel free to use" in the "About" section of
// the GitHub repository, and the code is licensed under the "Mulan PSL License" as well.
//

#pragma once

#include <QWidget>

namespace Gui::Widget {

class Battery : public QWidget
{
    Q_OBJECT

    Q_PROPERTY(ValueType minValue READ getMinValue WRITE setMinValue)
    Q_PROPERTY(ValueType maxValue READ getMaxValue WRITE setMaxValue)
    Q_PROPERTY(ValueType alarmValue READ getAlarmValue WRITE setAlarmValue)
    Q_PROPERTY(ValueType value READ getValue WRITE setValue)

    Q_PROPERTY(qreal borderWidth READ getBorderWidth WRITE setBorderWidth)
    Q_PROPERTY(qreal borderRadius READ getBorderRadius WRITE setBorderRadius)
    Q_PROPERTY(qreal backgroundRadius READ getBackgroundRadius WRITE setBackgroundRadius)
    Q_PROPERTY(qreal headRadius READ getHeadRadius WRITE setHeadRadius)

    Q_PROPERTY(QColor borderColor READ getBorderColor WRITE setBorderColor)
    Q_PROPERTY(QColor alarmColor READ getAlarmColor WRITE setAlarmColor)
    Q_PROPERTY(QColor normalColor READ getNormalColor WRITE setNormalColor)

    Q_PROPERTY(bool isCharging READ isCharging WRITE setCharging)
    Q_PROPERTY(bool isShowText READ isShowText WRITE setShowText)
    Q_PROPERTY(qreal textPadding READ getTextPadding WRITE setTextPadding)

public:
    using ValueType = quint32;

    explicit Battery(QWidget *parent = nullptr);

    ValueType getMinValue() const;
    ValueType getMaxValue() const;
    ValueType getAlarmValue() const;
    ValueType getValue() const;

    qreal getBorderWidth() const;
    qreal getBorderRadius() const;
    qreal getBackgroundRadius() const;
    qreal getHeadRadius() const;

    QColor getBorderColor() const;
    QColor getAlarmColor() const;
    QColor getNormalColor() const;
    QColor getChargingIconColor() const;

    bool isCharging() const;
    bool isShowText() const;
    qreal getTextPadding() const;

    // QSize sizeHint() const override;
    // QSize minimumSizeHint() const override;

public Q_SLOTS:
    void setRange(ValueType minValue, ValueType maxValue);

    void setMinValue(ValueType value);
    void setMaxValue(ValueType value);
    void setAlarmValue(ValueType value);
    void setValue(ValueType value);

    void setBorderWidth(qreal value);
    void setBorderRadius(qreal value);
    void setBackgroundRadius(qreal value);
    void setHeadRadius(qreal value);

    void setBorderColor(const QColor &value);
    void setAlarmColor(const QColor &value);
    void setNormalColor(const QColor &value);
    void setChargingIconColor(const QColor &value);

    void setCharging(bool value);
    void setShowText(bool value);
    void setTextPadding(qreal value);

    void setBatterySize(int width, int height);

Q_SIGNALS:
    void valueChanged(ValueType value);
    void chargingStateChanged(bool isCharging);

private:
    constexpr static inline qreal ChargingPadding = 3;

    ValueType _minValue{0};
    ValueType _maxValue{100};
    ValueType _alarmValue{20};
    ValueType _value{0};

    //
    // Zero means "derive it from the body height", which is how the proportions stay
    // correct at any size. A caller that sets one of these explicitly overrides that.
    //
    qreal _borderWidth{0};
    qreal _borderRadius{0};
    qreal _backgroundRadius{0};
    qreal _headRadius{0};

    //
    // Invalid means "the theme's", read when painting, so the ring follows a change of
    // mode. A caller that sets one keeps what it set.
    //
    QColor _borderColor{};
    QColor _normalColor{};
    QColor _alarmColor{};
    QColor _chargingIconColor{};

    bool _isCharging{false};
    bool _isShowText{true};
    // The reference sets the number close under the ring; the font's own leading adds
    // most of the remaining space.
    qreal _textPadding{6};

    QRectF _batteryRect{};
    QRectF _headRect{};
    QRectF _chargingRect{};
    QRectF _textRect{};

    QSizeF _batterySize{};

    // Recorded while the fill is drawn, so the charging bolt can be split across its edge.
    QRectF _fillRect{};
    QColor _fillColor{};

    void drawBorder(QPainter &painter);
    void drawBackground(QPainter &painter);
    void drawHead(QPainter &painter);
    void drawChargingIcon(QPainter &painter);
    void drawText(QPainter &painter);

    qreal getHeadWidth() const;
    qreal getChargingIconWidth() const;

protected:
    void paintEvent(QPaintEvent *event) override;
};
} // namespace Gui::Widget
