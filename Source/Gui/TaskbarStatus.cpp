// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Gui/TaskbarStatus.h"
#include "Source/Gui/TaskbarGeometry.h"

#include <QApplication>
#include <QDesktopWidget>
#include <QWindow>

#include "Source/Core/SystemTheme.h"
#include "Source/Support/OS/Windows.h"

//
// Windows 10
//
// +-------------------------------------------------------------------------------------------+
// |                                       Shell_TaryWnd                                       |
// |                  +---------------------------------------------------------+              |
// |                  |                      ReBarWindow32                      |              |
// |  Start and Pins  | +------------------------------------+ +--------------+ |  Tray Icons  |
// |                  | |           MSTaskSwWClass           | |  Our Window  | |              |
// |                  | +------------------------------------+ +--------------+ |              |
// |                  +---------------------------------------------------------+              |
// +-------------------------------------------------------------------------------------------+
//
// Windows 11
//
// +-------------------------------------------------------------------------------------------+
// |                                       Shell_TaryWnd                                       |
// | +--------------------------------------------------------------------------+              |
// | |                               ReBarWindow32                              |              |
// | |                  +------------------------------------+ +--------------+ |  Tray Icons  |
// | |  Start and Pins  |           MSTaskSwWClass           | |  Our Window  | |              |
// | |                  +------------------------------------+ +--------------+ |              |
// | +--------------------------------------------------------------------------+              |
// +-------------------------------------------------------------------------------------------+
//
// ----------
// * All these windows are the same height.
//

std::optional<TaskBarInfo> GetTaskBarInfo()
{
    std::optional<TaskBarInfo> result;

    do {
        HWND hShellTrayWnd = FindWindowW(L"Shell_TrayWnd", nullptr);
        if (hShellTrayWnd == nullptr) {
            LOG(Warn, "Find window 'Shell_TrayWnd' failed.");
            break;
        }

        HWND hReBarWindow32 = FindWindowExW(hShellTrayWnd, nullptr, L"ReBarWindow32", nullptr);
        if (hReBarWindow32 == nullptr) {
            LOG(Warn, "Find window 'ReBarWindow32' failed.");
            break;
        }

        HWND hMSTaskSwWClass = FindWindowExW(hReBarWindow32, nullptr, L"MSTaskSwWClass", nullptr);
        if (hMSTaskSwWClass == nullptr) {
            LOG(Warn, "Find window 'MSTaskSwWClass' failed.");
            break;
        }

        RECT rectShellTrayWnd{}, rectReBarWindow32{}, rectMSTaskSwWClass{},
            rectMSTaskSwWClassForParent{};

        if (!GetWindowRect(hShellTrayWnd, &rectShellTrayWnd)) {
            LOG(Warn, "Failed to get the rect of window 'Shell_TrayWnd'.");
            break;
        }

        if (!GetWindowRect(hReBarWindow32, &rectReBarWindow32)) {
            LOG(Warn, "Failed to get the rect of window 'ReBarWindow32'.");
            break;
        }

        if (!GetWindowRect(hMSTaskSwWClass, &rectMSTaskSwWClass)) {
            LOG(Warn, "Failed to get the rect of window 'MSTaskSwWClass'.");
            break;
        }

        rectMSTaskSwWClassForParent = rectMSTaskSwWClass;
        if (MapWindowPoints(
                HWND_DESKTOP, hReBarWindow32, (POINT *)&rectMSTaskSwWClassForParent, 2) == 0)
        {
            LOG(Warn, "Failed to get the rect of the parent of window 'MSTaskSwWClass'.");
            break;
        }

        using Support::OS::Windows::Window::RectToQRect;

        auto qrectShellTrayWnd = RectToQRect(rectReBarWindow32);

        result = TaskBarInfo{
            .isHorizontal = qrectShellTrayWnd.width() > qrectShellTrayWnd.height(),
            .hShellTrayWnd = hShellTrayWnd,
            .hReBarWindow32 = hReBarWindow32,
            .hMSTaskSwWClass = hMSTaskSwWClass,
            .rectReBarWindow32 = RectToQRect(rectReBarWindow32),
            .rectMSTaskSwWClass = RectToQRect(rectMSTaskSwWClass),
            .rectMSTaskSwWClassForParent = RectToQRect(rectMSTaskSwWClassForParent),
        };

    } while (false);

    return result;
}

namespace Gui {

TaskbarStatus::TaskbarStatus(QWidget *parent) : QDialog{parent}
{
    using Utils::Qt::SetPaletteColor;

    _ui.setupUi(this);

    _isWin11OrGreater = Support::OS::Windows::System::Is11OrGreater();
    LOG(Info, "Is Windows 11 or greater: '{}'", _isWin11OrGreater);

    connect(this, &TaskbarStatus::OnSettingsChangedSafely, this, &TaskbarStatus::OnSettingsChanged);

    _updateTimer.callOnTimeout([this] { OnUpdateTimer(); });

    //
    // `Qt::FramelessWindowHint` will cause a qt internal error:
    //
    // "UpdateLayeredWindowIndirect failed for ptDst=(1346, 0), size=(80x40), dirty=(80x40 0, 0)
    // (The parameter is incorrect.)"
    //
    // It is printed at
    // https://code.qt.io/cgit/qt/qtbase.git/tree/src/plugins/platforms/windows/qwindowsbackingstore.cpp?h=v5.15.2#n108
    //
    // We remove the flag `Qt::FramelessWindowHint` to make it call `BitBlt` instead of
    // `UpdateLayeredWindowIndirect`, and probably thanks to the later `setParent` call (I guess),
    // the actual effect will be the same as if the flag was set.
    //
    setWindowFlags(
        windowFlags() | Qt::Tool /* | Qt::FramelessWindowHint*/ | Qt::WindowStaysOnTopHint);

    layout()->setContentsMargins(5, 5, 5, 5);

    _icon.left->SetText("L");
    _icon.right->SetText("R");
    _battery.left->setShowText(false);
    _battery.left->setBatterySize(15, 15);
    _battery.right->setShowText(false);
    _battery.right->setBatterySize(15, 15);

    _ui.horizontalLayoutLeft->insertWidget(0, _icon.left);
    _ui.horizontalLayoutLeft->addWidget(_battery.left);
    _ui.horizontalLayoutRight->insertWidget(0, _icon.right);
    _ui.horizontalLayoutRight->addWidget(_battery.right);

    _taskbarDark = Core::SystemTheme::IsTaskbarDark();
    ApplyTaskbarTheme();
}

TaskbarStatus::~TaskbarStatus()
{
    _isStateReady = false;
    UpdateVisible();
}

void TaskbarStatus::SetTaskbarDark(bool dark)
{
    if (_taskbarDark == dark) {
        return;
    }
    _taskbarDark = dark;
    ApplyTaskbarTheme();
}

void TaskbarStatus::ApplyTaskbarTheme()
{
    using Utils::Qt::SetPaletteColor;

    // Follow Windows' taskbar theme (`SystemUsesLightTheme`) so text and icons stay readable
    // on both light and dark taskbars, independent of the app's own light/dark mode.
    const QColor fg = _taskbarDark ? QColor{245, 245, 247} : QColor{28, 28, 30};
    const QColor badgeFg = _taskbarDark ? QColor{28, 28, 30} : QColor{245, 245, 247};
    const QColor track = _taskbarDark ? QColor{90, 90, 96} : QColor{195, 195, 200};

    SetPaletteColor(_ui.labelLeft, QPalette::WindowText, fg);
    SetPaletteColor(_ui.labelRight, QPalette::WindowText, fg);

    for (auto *icon : {_icon.left, _icon.right}) {
        icon->SetBgColor(fg);
        icon->SetFgColor(badgeFg);
    }

    for (auto *battery : {_battery.left, _battery.right}) {
        battery->setBorderColor(track);
        battery->setChargingIconColor(fg);
    }

    update();
}

void TaskbarStatus::OnTaskbarCreated()
{
    // Explorer restarted and replaced the taskbar HWND: drop the stale foreign parent and
    // re-attach if the widget should currently be visible.
    if (_isActuallyEnabled) {
        hide();
        _updateTimer.stop();
        if (windowHandle() != nullptr) {
            windowHandle()->setParent(nullptr);
        }
        _rebarWindow.reset();
        _isActuallyEnabled = false;
    }
    UpdateVisible();
}

void TaskbarStatus::UpdateState(const Core::AirPods::State &state)
{
    _status = Status::Updating;
    _airPodsState = state;
    Repaint();
}

void TaskbarStatus::SetToolTip(const QString &text)
{
    setToolTip(text);
}

void TaskbarStatus::UpdateVisible()
{
    if (_behavior != TaskbarStatusBehavior::Disable && _isStateReady) {
        if (!_isActuallyEnabled) {
            if (Enable()) {
                _isActuallyEnabled = true;
            }
        }
    }
    else {
        if (_isActuallyEnabled) {
            if (Disable()) {
                _isActuallyEnabled = false;
            }
        }
    }
}

bool TaskbarStatus::Enable()
{
    const auto optInfo = GetTaskBarInfo();
    if (!optInfo.has_value()) {
        LOG(Error, "Try to enable, but failed to `GetTaskBarInfo()`");
        return false;
    }
    const auto &info = optInfo.value();

    winId(); // makes `windowHandle()` have a value
    _rebarWindow.reset(QWindow::fromWinId(reinterpret_cast<WId>(info.hReBarWindow32)));
    windowHandle()->setParent(_rebarWindow.get());
    setAttribute(Qt::WA_TranslucentBackground);
    UpdatePos(info, true);
    _isFirstTimeout = true;
    _updateTimer.start(kInitialUpdateInterval);
    show();
    return true;
}

bool TaskbarStatus::Disable()
{
    const auto optInfo = GetTaskBarInfo();
    hide();
    _updateTimer.stop();
    if (windowHandle() != nullptr) {
        windowHandle()->setParent(nullptr);
    }
    _rebarWindow.reset();

    if (!optInfo.has_value()) {
        LOG(Error, "Try to disable, but failed to `GetTaskBarInfo()`");
        return true;
    }
    UpdatePos(optInfo.value(), false);
    return true;
}

void TaskbarStatus::UpdatePos(const TaskBarInfo &info, bool enable)
{
    LOG(Trace, "The taskbar is '{}'", info.isHorizontal ? "horizontal" : "vertical");

    const auto &rectMSTaskSwWClassForParent = info.rectMSTaskSwWClassForParent;
    const auto &rectReBarWindow32 = info.rectReBarWindow32;
    const auto dpi = static_cast<int>(GetDpiForWindow(info.hReBarWindow32));
    const auto layout = TaskbarGeometry::CalculateLayout(
        rectReBarWindow32.size(), rectMSTaskSwWClassForParent, dpi, info.isHorizontal,
        QSize{kFixedWidth, kFixedHeight});

    if (enable) {
        if (!_isWin11OrGreater) {
            const auto &taskButtons = layout.taskButtonsNative;
            MoveWindow(
                info.hMSTaskSwWClass, taskButtons.left(), taskButtons.top(), taskButtons.width(),
                taskButtons.height(), true);
        }

        move(layout.statusLogical.topLeft());
        setFixedSize(layout.statusLogical.size());
        _cachedLength = info.isHorizontal ? rectReBarWindow32.width() : rectReBarWindow32.height();
    }
    else {
        if (!_isWin11OrGreater) {
            const auto restored = TaskbarGeometry::RestoreTaskButtons(
                rectReBarWindow32.size(), rectMSTaskSwWClassForParent, info.isHorizontal);
            MoveWindow(
                info.hMSTaskSwWClass, restored.left(), restored.top(), restored.width(),
                restored.height(), true);
        }
    }
}

void TaskbarStatus::Unavailable()
{
    _status = Status::Unavailable;
    _airPodsState.reset();
    Repaint();
}

void TaskbarStatus::Disconnect()
{
    _status = Status::Disconnected;
    _airPodsState.reset();
    Repaint();
}

void TaskbarStatus::Unbind()
{
    _status = Status::Unbind;
    _airPodsState.reset();
    Repaint();
}

void TaskbarStatus::Repaint()
{
    switch (_status) {
    case Status::Unavailable:
    case Status::Disconnected:
    case Status::Unbind: {
        _isStateReady = false;
        break;
    }
    case Status::Updating: {
        if (!_airPodsState.has_value()) {
            _isStateReady = false;
            break;
        }
        const auto &state = _airPodsState.value();

        // A one-piece device has one level and no sides: it takes the left slot without
        // the "L", and the right slot stays empty.
        using Core::AirPods::Details::BasicState;
        const bool onePiece = Core::AirPods::IsOnePiece(state.model);
        const BasicState left = onePiece ? Core::AirPods::OnePieceState(state.pods)
                                         : static_cast<const BasicState &>(state.pods.left);
        const BasicState right =
            onePiece ? BasicState{} : static_cast<const BasicState &>(state.pods.right);

        if (left.battery.Available()) {
            const auto batteryValue = left.battery.Value();

            _ui.labelLeft->setText(QString{"%1%"}.arg(batteryValue));
            _battery.left->setValue(batteryValue);
            _battery.left->setCharging(left.isCharging);

            _icon.left->setVisible(!onePiece);
            if (_behavior == TaskbarStatusBehavior::Text) {
                _ui.labelLeft->show();
                _battery.left->hide();
            }
            else {
                _ui.labelLeft->hide();
                _battery.left->show();
            }
        }
        else {
            _icon.left->hide();
            _ui.labelLeft->hide();
            _battery.left->hide();
        }

        if (right.battery.Available()) {
            const auto batteryValue = right.battery.Value();

            _ui.labelRight->setText(QString{"%1%"}.arg(batteryValue));
            _battery.right->setValue(batteryValue);
            _battery.right->setCharging(right.isCharging);

            _icon.right->show();
            if (_behavior == TaskbarStatusBehavior::Text) {
                _ui.labelRight->show();
                _battery.right->hide();
            }
            else {
                _ui.labelRight->hide();
                _battery.right->show();
            }
        }
        else {
            _icon.right->hide();
            _ui.labelRight->hide();
            _battery.right->hide();
        }

        _isStateReady = true;
        break;
    }
    default:
        APB_ASSERT(false);
    }

    UpdateVisible();
}

void TaskbarStatus::OnUpdateTimer()
{
    const auto optInfo = GetTaskBarInfo();
    if (!optInfo.has_value()) {
        LOG(Trace, "Try to update, but failed to `GetTaskBarInfo()`");
        return;
    }
    const auto &info = optInfo.value();

    bool taskbarResized = _cachedLength != (info.isHorizontal ? info.rectReBarWindow32.width()
                                                              : info.rectReBarWindow32.height());
    bool needToUpdate = taskbarResized || _isFirstTimeout;

    // We update the position again at the second time after the window is displayed, because the
    // first update may cause some shifting, I guess it's `setParent` causing some weird Qt bugs.
    if (_isFirstTimeout) {
        _isFirstTimeout = false;
        _updateTimer.setInterval(kIdleUpdateInterval);
    }

    if (taskbarResized) {
        LOG(Info, "Taskbar resized, status window need to update position");
    }

    if (needToUpdate) {
        UpdatePos(info, true);
    }
}

void TaskbarStatus::OnSettingsChanged(TaskbarStatusBehavior value)
{
    _behavior = value;
    Repaint();
}

void TaskbarStatus::paintEvent(QPaintEvent *event)
{
    QPainter painter{this};

#if defined APB_DEBUG
    if (_drawDebugBorder) {
        painter.setPen(QPen{Qt::red, 1.5f});
        painter.drawRect(rect());
    }
#endif

    //
    // Make transparent areas accept mouse events (only if we have not `setParent`)
    //
    // painter.setPen(Qt::NoPen);
    // painter.setBrush(Qt::black);
    // painter.setOpacity(0.5);
    // painter.drawRect(rect());

    QDialog::paintEvent(event);
}

void TaskbarStatus::mouseReleaseEvent(QMouseEvent *event)
{
    const auto button = event->button();

    if (button == Qt::LeftButton) {
#if defined APB_DEBUG
        _drawDebugBorder = !_drawDebugBorder;
        LOG(Debug, "_drawDebugBorder: {}", _drawDebugBorder);
        repaint();
#endif
        emit ShowMainWindowRequested();
    }
    else if (button == Qt::RightButton) {
        emit ShowTrayMenuRequested(event->globalPos());
    }

    QDialog::mouseReleaseEvent(event);
}

} // namespace Gui
