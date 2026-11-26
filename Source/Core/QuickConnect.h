// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

#include <QMetaType>
#include <QObject>
#include <QString>
#include <QTimer>

namespace Core::QuickConnect {

class Controller;

enum class Outcome : uint32_t {
    Disabled,
    NoDevice,          // nothing is configured yet
    DeviceUnavailable, // configured, but it is not reporting an audio endpoint right now
    AlreadyConnected,
    RequestStarted,
    Failed,
    Connected,
    TimedOut,
    Disconnecting,
    Disconnected,
    AlreadyDisconnected,
};

struct Device {
    QString id;
    QString name;
    bool connected{};
};

class Backend
{
public:
    virtual ~Backend() = default;

    virtual void SetController(Controller *controller);
    virtual std::vector<Device> ListDevices(std::stop_token stopToken) = 0;
    virtual bool RequestReconnect(const QString &id, std::stop_token stopToken) = 0;
    virtual bool RequestDisconnect(const QString &id, std::stop_token stopToken)
    {
        Q_UNUSED(id);
        Q_UNUSED(stopToken);
        return false;
    }

    // Called once the Controller stops caring about endpoint-state changes for the device it last
    // asked to reconnect or disconnect. A backend that arms system notifications must disarm them
    // here, otherwise it keeps doing that work for every device on the machine until exit.
    virtual void StopObserving() {}
    // Clears any cached device enumeration (for example after the Bluetooth adapter resets).
    virtual void InvalidateCache() {}
};

class NullBackend final : public Backend
{
public:
    std::vector<Device> ListDevices(std::stop_token) override
    {
        return {};
    }
    bool RequestReconnect(const QString &, std::stop_token) override
    {
        return false;
    }
};

class Controller final : public QObject
{
    Q_OBJECT

public:
    // How long to wait for the endpoint to change state after Windows accepted the request.
    static constexpr std::chrono::milliseconds kDefaultResolveTimeout{10'000};

    // The backend is shared rather than borrowed because a reconnect request can still be inside a
    // blocking driver call when the Controller goes away; that worker is detached and keeps the
    // backend alive for as long as it needs it.
    //
    // `resolveTimeout` is a parameter so tests can drive the real timer instead of calling the
    // timeout slot by hand.
    explicit Controller(
        std::shared_ptr<Backend> backend,
        std::chrono::milliseconds resolveTimeout = kDefaultResolveTimeout);
    ~Controller() override;

    void SetEnabled(bool enabled);
    bool IsEnabled() const;
    void SetDeviceId(QString id);
    const QString &DeviceId() const;
    bool HasDevice() const;
    std::vector<Device> Devices();
    QString DeviceNameFor(const QString &id) const;
    void RefreshDevices();
    void InvalidateCache();
    // Called when the bound Bluetooth device reports a live connection state change so a pending
    // Connect/Disconnect request resolves immediately even if WASAPI already activated earlier.
    void NotifyBluetoothConnected(bool connected);

    // A click on the tray icon. Reports every outcome, including why nothing happened.
    Outcome Request();
    // Explicit connect request (e.g. popup button or tray menu) that works regardless of
    // whether single-click tray quick connect is toggled on.
    Outcome RequestConnect();
    // Explicit disconnect request (e.g. tray menu) using `KSPROPERTY_ONESHOT_DISCONNECT`.
    Outcome RequestDisconnect();
    // Connects if currently disconnected, disconnects if currently connected.
    Outcome RequestToggle();
    // The automation engine asking, because the case opened nearby. Ignores the tray
    // click's on/off switch, and is silent when there is nothing to do.
    Outcome RequestAutomatic();

    Outcome OnEndpointStateChanged(const QString &id, bool connected);
    Outcome ResolveTimedOut();

signals:
    void DevicesChanged();
    void OutcomeChanged(Core::QuickConnect::Outcome outcome, QString deviceName);

private:
    enum class RequestKind { Connect, Disconnect, Toggle };

    // Lets a worker post back without ever dereferencing a Controller that has started tearing
    // down. `~Controller` clears the pointer under the mutex before anything else happens, so a
    // worker either posts to a live Controller or finds nothing and gives up.
    struct Link {
        std::mutex mutex;
        Controller *controller{};
    };

    std::shared_ptr<Backend> _backend;
    std::shared_ptr<Link> _link{std::make_shared<Link>()};
    bool _enabled{};
    bool _pending{};
    bool _pendingExpectConnected{true};
    QString _deviceId;
    QString _pendingDeviceId;
    QString _pendingDeviceName;
    QString _requestingDeviceId;
    bool _connectedDuringRequest{};
    bool _disconnectedDuringRequest{};
    std::vector<Device> _devices;
    std::atomic<bool> _deviceRefreshRunning{};
    std::atomic<bool> _requestRunning{};
    std::jthread _deviceRefreshThread;
    std::stop_source _requestStop;
    std::thread _requestThread;
    QTimer _resolveTimer;

    static void
    PostToController(const std::shared_ptr<Link> &link, std::function<void(Controller &)> work);

    Outcome Start(RequestKind kind, bool automatic, bool requireTraySwitch);
    Outcome Complete(Outcome outcome);
    void EmitOutcome(Outcome outcome, const QString &deviceName);
};

} // namespace Core::QuickConnect

Q_DECLARE_METATYPE(Core::QuickConnect::Outcome)
