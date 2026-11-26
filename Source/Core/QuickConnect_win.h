// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#if !defined APB_OS_WIN
    #error "This file shouldn't be compiled."
#endif

#include "Source/Core/QuickConnect.h"
#include "Source/Support/OS/Windows.h"

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>

#include <mmdeviceapi.h>

namespace Core::QuickConnect {

class WindowsBackend final : public Backend, public IMMNotificationClient
{
public:
    WindowsBackend();
    ~WindowsBackend() override;

    void SetController(Controller *controller) override;
    std::vector<Device> ListDevices(std::stop_token stopToken) override;
    bool RequestReconnect(const QString &id, std::stop_token stopToken) override;
    bool RequestDisconnect(const QString &id, std::stop_token stopToken) override;
    void StopObserving() override;
    void InvalidateCache() override;

    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **object) override;

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR deviceId, DWORD newState) override;
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR deviceId) override;
    HRESULT STDMETHODCALLTYPE
    OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR defaultDeviceId) override;
    HRESULT STDMETHODCALLTYPE
    OnPropertyValueChanged(LPCWSTR deviceId, const PROPERTYKEY key) override;

private:
    // What `OnDeviceStateChanged` needs in order to decide, without touching COM.
    //
    // The MMDevice API forbids a notification callback from blocking, from releasing the final
    // reference on an MMDevice object, and from calling back into the API. Resolving an endpoint's
    // container id inside the callback would do all three, so `RequestReconnect` resolves the
    // endpoint ids up front and the callback only compares strings against this snapshot.
    struct Observation {
        QString containerId;           // canonical `{GUID}`, handed to the Controller
        std::set<QString> endpointIds; // exact IMMDevice ids belonging to that container
    };

    Support::OS::Windows::Com::UniquePtr<IMMDeviceEnumerator> _deviceEnumerator;
    std::atomic<std::shared_ptr<const Observation>> _observation;
    Controller *_controller{};
    std::atomic<ULONG> _refCount{1};
    std::mutex _controllerMutex;
    std::mutex _pairedCacheMutex;
    std::map<QString, Device> _pairedCache;
    std::chrono::steady_clock::time_point _pairedCacheUpdated{};
    bool _comInitialized{};

    void RegisterNotifications();
    void UnregisterNotifications();
    bool SendContainerProperty(const QString &id, ULONG propertyId, std::stop_token stopToken);
    void QueueEndpointState(const QString &containerId, bool connected);
};

} // namespace Core::QuickConnect
