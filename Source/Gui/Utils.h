// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QTimer>
#include <QWidget>
#include <QtMath>

#include "Source/Support/Utils.h"

namespace Utils::Qt {

#define UTILS_QT_DISABLE_ESC_QUIT(base_name)                                                       \
    inline void keyPressEvent(QKeyEvent *event) override                                           \
    {                                                                                              \
        if (event->key() == Qt::Key_Escape) {                                                      \
            event->accept();                                                                       \
        }                                                                                          \
        else {                                                                                     \
            base_name::keyPressEvent(event);                                                       \
        }                                                                                          \
    }

inline void SetPaletteColor(QWidget *widget, QPalette::ColorRole colorRole, const QColor &color)
{
    auto palette = widget->palette();
    palette.setColor(colorRole, color);
    widget->setPalette(palette);
}

inline QColor InvertColor(const QColor &color)
{
    QColor result;
    result.setRgb(255 - color.red(), 255 - color.green(), 255 - color.blue());
    return result;
}

inline void Dispatch(std::function<void()> callback)
{
    QTimer *timer = new QTimer;
    timer->moveToThread(qApp->thread());
    timer->setSingleShot(true);
    QObject::connect(timer, &QTimer::timeout, [timer, callback = std::move(callback)]() {
        callback();
        timer->deleteLater();
    });
    QMetaObject::invokeMethod(timer, "start", ::Qt::QueuedConnection, Q_ARG(int, 0));
}

inline void QuitApplicationSafely()
{
    QMetaObject::invokeMethod(qApp, &QApplication::quit, ::Qt::QueuedConnection);
}

} // namespace Utils::Qt
