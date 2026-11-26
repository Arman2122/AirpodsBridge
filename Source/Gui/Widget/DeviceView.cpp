// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/Widget/DeviceView.h"

#include <QPainter>

#include "Source/Core/WindowEffects.h"
#include "Source/Gui/FrameClock.h"

namespace Gui::Widget {

namespace {

// Frames decoded in one tick when the loop has fallen behind. The file decodes forward
// only, so catching up means decoding the frames in between; past this many the time is
// written off instead and the loop carries on from the frame it reached.
constexpr int kCatchUpBudget = 6;

} // namespace

DeviceView::DeviceView(QWidget *parent) : QWidget{parent}
{
    setAttribute(Qt::WA_TranslucentBackground);
#if defined APB_DEBUG
    setCursor(Qt::PointingHandCursor);
#endif
}

DeviceView::~DeviceView()
{
    // Destroyed mid-loop: the hold on the clock goes with the widget.
    Stop();
}

void DeviceView::SetArtwork(const std::optional<DeviceArtwork::Artwork> &artwork)
{
    if (artwork == _artwork) {
        return;
    }
    Stop();
    _artwork = artwork;
    _still = {};
    Fit();
    if (isVisible()) {
        Start();
    }
    update();
}

QRect DeviceView::ContentRect() const
{
    return _target;
}

void DeviceView::Fit()
{
    const QRect previous = _target;
    if (!_artwork.has_value() || _artwork->size.isEmpty() || rect().isEmpty()) {
        _target = {};
        _devicePixels = {};
        if (_target != previous) {
            Q_EMIT ContentRectChanged();
        }
        return;
    }

    // Aspect-fit into the slot; everything is prepared at device resolution once, rather
    // than scaled on every paint.
    const QSize fitted = _artwork->size.scaled(rect().size(), Qt::KeepAspectRatio);
    _target = QRect{QPoint{}, fitted};
    _target.moveCenter(rect().center());

    const qreal dpr = devicePixelRatioF();
    _devicePixels = fitted * dpr;

    _still = QPixmap{_artwork->still}.scaled(
        _devicePixels, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    _still.setDevicePixelRatio(dpr);

    if (_playing) {
        // A new size mid-loop: frames are decoded at it from here on.
        OpenLoop();
    }
    if (_target != previous) {
        Q_EMIT ContentRectChanged();
    }
}

void DeviceView::OpenLoop()
{
    _reader = std::make_unique<QImageReader>(_artwork->loop);
    // Frames are decoded at the fitted size and never kept: the loop is long and the
    // widget is small, and keeping every frame would cost more than the popup itself.
    _reader->setScaledSize(_devicePixels);
    _decodedFrame = -1;
}

void DeviceView::Start()
{
    if (_playing || !_artwork.has_value() || _artwork->loop.isEmpty() || _artwork->fps <= 0 ||
        _artwork->frames <= 0 || _devicePixels.isEmpty())
    {
        return;
    }
    // An accessibility setting, not a preference: a user who turned animations off gets
    // the still.
    if (!Core::WindowEffects::AreAnimationsEnabled()) {
        return;
    }
    // The clock every animation runs on: one tick per frame of the display. A tool that
    // made none gets the still.
    auto *clock = FrameClock::Instance();
    if (clock == nullptr) {
        return;
    }

    OpenLoop();
    _frame = {};
    _shownFrame = -1;
    _forgivenMs = 0;
    _elapsed.start();

    _frameConnection = connect(clock, &FrameClock::Frame, this, &DeviceView::Tick);
    clock->Retain();
    _playing = true;
    Tick();
}

void DeviceView::Stop()
{
    if (_playing) {
        disconnect(_frameConnection);
        if (auto *clock = FrameClock::Instance(); clock != nullptr) {
            clock->Release();
        }
        _playing = false;
    }
    _reader.reset();
    _frame = {};
    _decodedFrame = -1;
    _shownFrame = -1;
    update();
}

void DeviceView::Tick()
{
    if (_reader == nullptr) {
        return;
    }

    const qint64 elapsed = _elapsed.elapsed() - _forgivenMs;
    const int due = static_cast<int>((elapsed * _artwork->fps / 1000) % _artwork->frames);
    if (due == _shownFrame) {
        return;
    }

    // A wrap reopens the file. Frames on the way to the due one are decoded and
    // discarded: dropped, never shown late.
    if (due < _decodedFrame) {
        OpenLoop();
    }
    QImage image;
    int budget = kCatchUpBudget;
    while (_decodedFrame < due && budget-- > 0) {
        image = _reader->read();
        if (image.isNull()) {
            // A file that will not decode: the still stays.
            Stop();
            return;
        }
        ++_decodedFrame;
    }
    if (_decodedFrame < due) {
        // Too far behind for one tick, after a stall: the gap is written off and the loop
        // carries on from the frame it reached.
        _forgivenMs += static_cast<qint64>(due - _decodedFrame) * 1000 / _artwork->fps;
    }
    if (image.isNull()) {
        return;
    }
    _frame = QPixmap::fromImage(std::move(image));
    _frame.setDevicePixelRatio(devicePixelRatioF());
    _shownFrame = _decodedFrame;
    update();
}

void DeviceView::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    if (_target.isEmpty()) {
        return;
    }
    QPainter painter{this};
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawPixmap(_target.topLeft(), _frame.isNull() ? _still : _frame);
}

void DeviceView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    Fit();
}

void DeviceView::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The window may have moved to a display with another scale since it was last shown.
    Fit();
    Start();
}

void DeviceView::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    Stop();
}

void DeviceView::mouseReleaseEvent(QMouseEvent *event)
{
    Q_UNUSED(event);
    Q_EMIT Clicked();
}

} // namespace Gui::Widget
