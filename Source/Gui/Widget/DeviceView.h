// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <memory>
#include <optional>

#include <QElapsedTimer>
#include <QImageReader>
#include <QPixmap>
#include <QWidget>

#include "Source/Presentation/DeviceArtwork.h"

namespace Gui::Widget {

//
// Shows a device the way the iPhone's card does: Apple's transparent render, animated
// when the artwork has a loop, floating on whatever is behind the widget.
//
// The loop is paced by the clock, not by the decoder. On every frame of the display
// (`Gui::FrameClock`) the frame that is due is worked out from the time elapsed, decoded,
// and shown; a frame that comes late is skipped rather than shown late, so the loop takes
// exactly as long as Apple made it, on any display and however slow the machine.
//
// The still is always there, so the slot is never blank: it is what shows before the first
// frame, while the loop is stopped, when Windows' animation setting is off, and in a tool
// that made no frame clock. Nothing decodes while the widget is hidden; a hidden popup
// costs nothing.
//
class DeviceView : public QWidget
{
    Q_OBJECT

public:
    explicit DeviceView(QWidget *parent = nullptr);
    ~DeviceView() override;

    // Nothing is shown for `std::nullopt`. Setting the same artwork again is free.
    void SetArtwork(const std::optional<DeviceArtwork::Artwork> &artwork);

    // Where the artwork lands in the widget, aspect-fitted; empty without artwork. What
    // is drawn in relation to the picture -- the battery rings -- is placed from this and
    // the artwork's anchors.
    QRect ContentRect() const;

Q_SIGNALS:
    void Clicked();
    // `ContentRect()` moved: the widget was resized, or shown on a display with another
    // scale. Whatever is placed from it is placed again.
    void ContentRectChanged();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    std::optional<DeviceArtwork::Artwork> _artwork;
    QRect _target;                         // where the artwork lands, in logical pixels
    QSize _devicePixels;                   // its size in device pixels
    QPixmap _still;                        // fitted to the slot
    QPixmap _frame;                        // the loop frame on show; null before the first
    std::unique_ptr<QImageReader> _reader; // the loop, decoded a frame at a time
    int _decodedFrame{-1};                 // the reader's last frame; -1 at the start
    int _shownFrame{-1};                   // the frame `_frame` holds
    bool _playing{false};                  // holding the frame clock
    QMetaObject::Connection _frameConnection;
    QElapsedTimer _elapsed; // time since the loop started
    qint64 _forgivenMs{0};  // time written off after a stall too long to catch up

    void Fit();
    void Start();
    void Stop();
    void OpenLoop();
    void Tick();
};

} // namespace Gui::Widget
