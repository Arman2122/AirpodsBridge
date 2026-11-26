// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <memory>
#include <optional>

#include <QDialog>
#include <QWindow>

#include "ui_TaskbarStatus.h"

#include "Source/Core/AirPods.h"
#include "Source/Core/Settings.h"
#include "Source/Gui/Utils.h"
#include "Source/Gui/Widget/Battery.h"
#include "Source/Presentation/Base.h"

struct TaskBarInfo {
    bool isHorizontal;
    HWND hShellTrayWnd, hReBarWindow32, hMSTaskSwWClass;
    QRect rectReBarWindow32, rectMSTaskSwWClass, rectMSTaskSwWClassForParent;
};

namespace Gui {

using namespace std::chrono_literals;

using Core::Settings::TaskbarStatusBehavior;

class MiniIcon : public QWidget
{
    Q_OBJECT

public:
    inline MiniIcon(QWidget *parent = nullptr) : QWidget{parent}
    {
        setFixedSize(kSize, kSize);

        _textFont = font();
        _textFont.setPointSize(_textFont.pointSize() - 2);
    }

    inline void SetText(QString text)
    {
        _text = std::move(text);
        update();
    }

    inline void SetBgColor(QColor color)
    {
        _bgColor = std::move(color);
        update();
    }

    inline void SetFgColor(QColor color)
    {
        _fgColor = std::move(color);
        update();
    }

private:
    constexpr static inline auto kSize{12};

    QString _text;
    QFont _textFont;
    QColor _bgColor{Qt::white}, _fgColor{Qt::black};

protected:
    inline void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter{this};
        painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);

        DrawBackground(painter);
        DrawText(painter);
    }

    inline void DrawBackground(QPainter &painter)
    {
        painter.save();
        {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QBrush{_bgColor});
            painter.drawEllipse(rect());
        }
        painter.restore();
    }

    inline void DrawText(QPainter &painter)
    {
        painter.save();
        {
            painter.setPen(_fgColor);
            painter.setBrush(Qt::NoBrush);
            painter.setFont(_textFont);

            painter.drawText(rect(), Qt::AlignCenter, _text);
        }
        painter.restore();
    }
};

class TaskbarStatus : public QDialog
{
    Q_OBJECT

public:
    TaskbarStatus(QWidget *parent = nullptr);
    ~TaskbarStatus();

    void UpdateState(const Core::AirPods::State &state);
    void Unavailable();
    void Disconnect();
    void Unbind();
    void SetToolTip(const QString &text);
    void SetTaskbarDark(bool dark);
    void OnTaskbarCreated();

Q_SIGNALS:
    void OnSettingsChangedSafely(TaskbarStatusBehavior value);
    void ShowMainWindowRequested();
    void ShowTrayMenuRequested(const QPoint &position);

private:
    constexpr static inline auto kFixedWidth{60};  // for horizontal taskbar
    constexpr static inline auto kFixedHeight{40}; // for vertical taskbar

    constexpr static inline auto kInitialUpdateInterval{100ms};
    constexpr static inline auto kIdleUpdateInterval{2s};

    Ui::TaskbarStatus _ui;
    Helper::Sides<MiniIcon *> _icon = {new MiniIcon{this}, new MiniIcon{this}};
    Helper::Sides<Widget::Battery *> _battery = {
        new Widget::Battery{this}, new Widget::Battery{this}};
    std::unique_ptr<QWindow> _rebarWindow;
    TaskbarStatusBehavior _behavior{TaskbarStatusBehavior::Disable};
    bool _isWin11OrGreater{false}, _isActuallyEnabled{false}, _isStateReady{false},
        _isFirstTimeout{false}, _taskbarDark{true};
    int _cachedLength{0};
    QTimer _updateTimer;
    std::optional<Core::AirPods::State> _airPodsState;
    Status _status{Status::Unavailable};
#if defined APB_DEBUG
    bool _drawDebugBorder{false};
#endif

    void ApplyTaskbarTheme();
    void UpdateVisible();
    bool Enable();
    bool Disable();
    void UpdatePos(const TaskBarInfo &info, bool enable);
    void Repaint();

    void OnUpdateTimer();
    void OnSettingsChanged(TaskbarStatusBehavior value);

    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

    UTILS_QT_DISABLE_ESC_QUIT(QDialog);
};

} // namespace Gui
