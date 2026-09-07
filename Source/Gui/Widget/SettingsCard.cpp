// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/SettingsCard.h"

#include <QHBoxLayout>
#include <QPainter>

#include "Source/Gui/Painting.h"
#include "Source/Gui/Widget/Label.h"
#include "Source/Presentation/Theme.h"

namespace Gui::Widget {

//////////////////////////////////////////////////

SettingsRow::SettingsRow(
    const QString &title, const QString &description, QWidget *control, QWidget *parent)
    : QWidget{parent}, _title{new Label{title, Label::Role::Primary, this}},
      _description{new Label{description, Label::Role::Secondary, this}}
{
    setMinimumHeight(Theme::Metric::RowMinHeight);

    _title->setFont(Theme::Font::Body());

    _description->setFont(Theme::Font::Footnote());
    _description->setWordWrap(true);
    _description->setVisible(!description.isEmpty());

    auto *text = new QVBoxLayout;
    text->setContentsMargins(0, 0, 0, 0);
    text->setSpacing(2);
    text->addWidget(_title);
    text->addWidget(_description);

    auto *layout = new QHBoxLayout{this};
    layout->setContentsMargins(
        Theme::Metric::RowPaddingH, Theme::Metric::RowPaddingV, Theme::Metric::RowPaddingH,
        Theme::Metric::RowPaddingV);
    layout->setSpacing(Theme::Metric::RowPaddingH);
    layout->addLayout(text, 1);

    if (control != nullptr) {
        control->setParent(this);
        layout->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
}

void SettingsRow::SetTitle(const QString &title)
{
    _title->setText(title);
}

void SettingsRow::SetDescription(const QString &description)
{
    _description->setText(description);
    _description->setVisible(!description.isEmpty());
}

//////////////////////////////////////////////////

class SettingsGroup::Card : public QWidget
{
public:
    explicit Card(QWidget *parent) : QWidget{parent}, _rows{new QVBoxLayout{this}}
    {
        _rows->setContentsMargins(0, 0, 0, 0);
        _rows->setSpacing(0);
    }

    void Add(QWidget *widget)
    {
        _rows->addWidget(widget);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter{this};
        painter.setRenderHint(QPainter::Antialiasing);

        painter.setPen(Qt::NoPen);
        painter.setBrush(Theme::Colours().Card);
        painter.drawPath(Painting::ContinuousRoundedRect(
            QRectF{rect()}.adjusted(0.5, 0.5, -0.5, -0.5), Theme::Metric::CardCornerRadius));

        // A hairline under every row except the last, inset to the rows' text.
        painter.setPen(QPen{Theme::Colours().Divider, 1.0});
        const int count = _rows->count();
        for (int i = 0; i + 1 < count; ++i) {
            auto *item = _rows->itemAt(i);
            if (item->widget() == nullptr || !item->widget()->isVisible()) {
                continue;
            }
            const qreal y = item->geometry().bottom() + 0.5;
            painter.drawLine(
                QPointF{static_cast<qreal>(Theme::Metric::RowPaddingH), y},
                QPointF{static_cast<qreal>(width() - Theme::Metric::RowPaddingH), y});
        }
    }

private:
    QVBoxLayout *_rows;
};

SettingsGroup::SettingsGroup(const QString &heading, QWidget *parent)
    : QWidget{parent}, _card{new Card{this}}
{
    auto *layout = new QVBoxLayout{this};
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    if (!heading.isEmpty()) {
        auto *label = new Label{heading, Label::Role::Secondary, this};
        label->setFont(Theme::Font::Caption());
        label->setContentsMargins(Theme::Metric::RowPaddingH, 0, 0, 0);
        layout->addWidget(label);
    }

    layout->addWidget(_card);
}

SettingsRow *
SettingsGroup::AddRow(const QString &title, const QString &description, QWidget *control)
{
    auto *row = new SettingsRow{title, description, control, _card};
    _card->Add(row);
    return row;
}

void SettingsGroup::AddWidget(QWidget *widget)
{
    widget->setParent(_card);
    _card->Add(widget);
}

} // namespace Gui::Widget
