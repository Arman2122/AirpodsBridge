// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#include "Source/Core/SystemEvents.h"

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QCoreApplication>
#include <QWindow>

#include "Source/Support/Logger.h"
#include "Source/Support/OS/Windows.h"

namespace Core::SystemEvents {

class Watcher::Impl final : public QAbstractNativeEventFilter
{
public:
    explicit Impl(Callbacks callbacks)
        : _callbacks{std::move(callbacks)},
          _taskbarCreatedMessage{RegisterWindowMessageW(L"TaskbarCreated")}
    {
        // A realised hidden top-level window guarantees this process receives shell and
        // power broadcasts even when no visible AirpodsBridge window is open.
        _receiver.setFlags(Qt::Tool | Qt::FramelessWindowHint);
        _receiver.setGeometry(0, 0, 1, 1);
        _receiver.create();
        _receiverHwnd = reinterpret_cast<HWND>(_receiver.winId());

        QCoreApplication::instance()->installNativeEventFilter(this);
    }

    ~Impl() override
    {
        QCoreApplication::instance()->removeNativeEventFilter(this);
    }

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(eventType);
        Q_UNUSED(result);

        const auto *msg = static_cast<const MSG *>(message);
        // Filter to our dedicated receiver HWND so a broadcast delivered to multiple
        // top-level windows only fires each callback once.
        if (msg->hwnd != _receiverHwnd) {
            return false;
        }

        if (_taskbarCreatedMessage != 0 && msg->message == _taskbarCreatedMessage) {
            LOG(Info, "SystemEvents: TaskbarCreated received.");
            if (_callbacks.onTaskbarCreated) {
                _callbacks.onTaskbarCreated();
            }
            return false;
        }

        if (msg->message == WM_POWERBROADCAST) {
            if (msg->wParam == PBT_APMSUSPEND) {
                LOG(Info, "SystemEvents: system suspending.");
                if (_callbacks.onSuspend) {
                    _callbacks.onSuspend();
                }
            }
            else if (msg->wParam == PBT_APMRESUMEAUTOMATIC || msg->wParam == PBT_APMRESUMESUSPEND) {
                LOG(Info, "SystemEvents: system resumed.");
                if (_callbacks.onResume) {
                    _callbacks.onResume();
                }
            }
        }

        return false;
    }

private:
    Callbacks _callbacks;
    UINT _taskbarCreatedMessage{0};
    QWindow _receiver;
    HWND _receiverHwnd{nullptr};
};

Watcher::Watcher(Callbacks callbacks) : _impl{std::make_unique<Impl>(std::move(callbacks))} {}

Watcher::~Watcher() = default;

} // namespace Core::SystemEvents
