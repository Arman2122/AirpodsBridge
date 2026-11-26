// SPDX-FileCopyrightText: 2026 Arman2122
// SPDX-License-Identifier: PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string_view>
#include <utility>

#include <QObject>

#include "Source/Support/Helper.h"

namespace Core::Settings {

class Repository;

enum class TrayIconBatteryBehavior : uint32_t { Disable, WhenLowBattery, Always };
enum class TaskbarStatusBehavior : uint32_t { Disable, Text, Icon };
// In the order the settings window offers them. `System` follows Windows' app mode.
enum class Appearance : uint32_t { System, Light, Dark };
enum class LoadResult : uint32_t { AbiIncompatible, NoAbiField, Successful };

void SetRepository(std::unique_ptr<Repository> repository);

// TODO: in [v1.0.0] [kFieldsAbiVersion = 2]
//        - Rename `tray_icon_battery` to `battery_on_tray_icon`

//
// Every setting the application has, in one place.
//
// A line here is the whole definition of a setting: its type, the key it is stored under,
// its default, and the sentence the settings window shows for it. Nothing in this file
// knows who reacts to a change -- whoever does subscribes with `Watch` below, at the
// place where the reaction lives. Adding a setting is one line here and one `Watch`.
//
// clang-format off
#define SETTINGS_FIELDS(callback)                                                                  \
    callback(bool, auto_run, {false},                                                              \
        Impl::Desc{QObject::tr("Starts AirpodsBridge in the background when you sign in to Windows.")}) \
    callback(bool, low_audio_latency, {false}, Impl::Deprecated())                                 \
    callback(bool, automatic_ear_detection, {true},                                                \
        Impl::Desc{QObject::tr("It automatically pauses or resumes media when your AirPods are taken out or put in your ears.")}) \
    callback(QString, skipped_version, {})                                                         \
    callback(int16_t, rssi_min, {-80},                                                             \
        Impl::Desc{QObject::tr("How far away your AirPods can be and still be picked up. Reduce it if the app latches onto someone else's AirPods nearby.")}) \
    callback(bool, reduce_loud_sounds, {false}, Impl::Deprecated())                                \
    callback(uint32_t, loud_volume_level, {40}, Impl::Deprecated())                                \
    callback(uint64_t, device_address, {0}, Impl::Sensitive{})                                     \
    callback(QString, bound_device_name, {}, Impl::Sensitive{})                                    \
    callback(TrayIconBatteryBehavior, tray_icon_battery, {TrayIconBatteryBehavior::Disable},       \
        Impl::Desc{QObject::tr("Shows the lowest battery of the two AirPods as a number on the tray icon.")}) \
    callback(bool, tray_quick_connect_enabled, {false},                                            \
        Impl::Desc{QObject::tr("A click on the tray icon connects the selected device instead of opening the main window. Double-click still opens it.")}) \
    callback(QString, tray_quick_connect_device_id, {}, Impl::Sensitive{})                         \
    callback(TaskbarStatusBehavior, battery_on_taskbar, {TaskbarStatusBehavior::Disable},          \
        Impl::Desc{QObject::tr("Shows the lowest battery on the taskbar, as text or as a small icon.")}) \
    callback(bool, check_for_updates, {true},                                                      \
        Impl::Desc{QObject::tr("Checks GitHub Releases periodically for a new version. Nothing is downloaded or installed without asking first.")}) \
    callback(bool, show_popup_on_lid_open, {true},                                                 \
        Impl::Desc{QObject::tr("Opens the battery popup when the case lid is opened, the way an iPhone does.")}) \
    callback(bool, show_nearby_popup_when_disconnected, {true},                                    \
        Impl::Desc{QObject::tr("Shows an iPhone-style nearby popup with live battery and a one-click Connect button even when your AirPods are connected to another device.")}) \
    callback(bool, show_heart_rate_in_popup, {true},                                               \
        Impl::Desc{QObject::tr("Shows a compact live Apple Health Heart Rate bar inside the battery popup for AirPods Pro 3 and Powerbeats Pro 2.")}) \
    callback(bool, ear_detection_resume_playback, {true},                                          \
        Impl::Desc{QObject::tr("Also resumes media when your AirPods are back in your ears. Turn this off to only ever pause.")}) \
    callback(bool, ear_detection_single_bud, {false},                                              \
        Impl::Desc{QObject::tr("Treats one AirPod in an ear as wearing them: media pauses when the last one comes out and resumes when the first goes in.")}) \
    callback(bool, pause_on_disconnect, {true},                                                    \
        Impl::Desc{QObject::tr("Pauses media when your AirPods disconnect, so it does not carry on through the speakers.")}) \
    callback(bool, low_battery_notification, {true},                                               \
        Impl::Desc{QObject::tr("Shows a notification when an AirPod or the case drops to the level below. Once per drop.")}) \
    callback(uint32_t, low_battery_threshold, {20},                                                \
        Impl::Desc{QObject::tr("What counts as low, for the notification and for the tray icon's \"when low\" option.")}) \
    callback(bool, full_charge_notification, {false},                                              \
        Impl::Desc{QObject::tr("Shows a notification once your AirPods finish charging to 100%.")}) \
    callback(bool, auto_connect_on_lid_open, {false},                                              \
        Impl::Desc{QObject::tr("Connects the quick connect device when its case opens near this computer. Keeps Bluetooth scanning on while disconnected, which uses a little more power.")}) \
    callback(QString, preferred_mic_device_id, {}, Impl::Sensitive{},                              \
        Impl::Desc{QObject::tr("Keeps Windows' default microphone on a desktop mic so games and calls do not switch AirPods into muffled Hands-Free audio.")}) \
    callback(bool, auto_switch_audio_on_connect, {true},                                           \
        Impl::Desc{QObject::tr("Automatically switches Windows playback to your AirPods when they connect and restores your previous output device when they disconnect.")}) \
    callback(bool, smart_audio_routing, {false},                                                   \
        Impl::Desc{QObject::tr("Switches Windows playback to your AirPods when put in your ears and restores your previous output when taken out.")}) \
    callback(bool, volume_cap_enabled, {false},                                                    \
        Impl::Desc{QObject::tr("Limits AirPods playback volume when they connect so audio never starts unexpectedly loud.")}) \
    callback(uint32_t, volume_cap_level, {80},                                                     \
        Impl::Desc{QObject::tr("Maximum playback volume applied when AirPods connect.")})          \
    callback(bool, audio_keep_alive, {false},                                                      \
        Impl::Desc{QObject::tr("Locks the AirPods Windows audio engine to its 3 ms minimum period and keeps the stream awake while worn so game sounds, clicks, and notifications start with minimal delay.")}) \
    callback(QString, hotkey_toggle_connect, {},                                                   \
        Impl::Desc{QObject::tr("Global shortcut to connect or disconnect your AirPods from any app.")}) \
    callback(QString, hotkey_toggle_popup, {},                                                     \
        Impl::Desc{QObject::tr("Global shortcut to open or hide the battery popup.")})             \
    callback(QString, hotkey_cycle_noise_control, {},                                              \
        Impl::Desc{QObject::tr("Global shortcut to cycle Noise Control modes when two-way AAP control is active.")}) \
    callback(bool, native_toast_notifications, {true},                                             \
        Impl::Desc{QObject::tr("Uses Windows toast notifications when installed. Portable mode always uses tray alerts.")}) \
    callback(bool, radio_service_low_latency_qos, {true},                                          \
        Impl::Desc{QObject::tr("Upgrades AirPods ACL links to Guaranteed 3.75 ms QoS, clamps Sniff sleep intervals, and caps retransmit flush timeout to 40 ms via the Radio Service.")}) \
    callback(bool, radio_service_hfp_decline, {false},                                             \
        Impl::Desc{QObject::tr("Declines AirPods Hands-Free RFCOMM links at the Bluetooth HCI layer so games and calls never drop stereo playback into muffled mono.")}) \
    callback(bool, radio_service_virtual_mic, {false},                                             \
        Impl::Desc{QObject::tr("Exposes a stereo-safe virtual USB Audio Class 1.0 microphone via the Radio Service and Windows' built-in usbaudio driver.")}) \
    callback(Appearance, appearance, {Appearance::System},                                         \
        Impl::Desc{QObject::tr("Light or dark. System follows the mode Windows is set to, and changes with it.")})
// clang-format on

struct Fields {
#define DECLARE_FIELD(type, name, dft, ...) type name dft;
    SETTINGS_FIELDS(DECLARE_FIELD)
#undef DECLARE_FIELD
};

namespace Impl {

//////////////////////////////////////////////////
// Options a field can carry
//

class Desc
{
public:
    Desc() = default;
    Desc(QString description) : _description{std::move(description)} {}

    const QString &Description() const
    {
        return _description;
    }

private:
    QString _description;
};

// Sensitive: never written to the log in the clear. Deprecated: erased from the store on
// the next save, and its absence is not reported.
class Sensitive
{
};
class Deprecated
{
};

//////////////////////////////////////////////////

template <class T>
class MetaField
{
public:
    template <class... ArgsT>
    MetaField(std::string_view name, T member, ArgsT &&...args)
        : _name{std::move(name)}, _member{std::move(member)}
    {
        std::initializer_list<int> ignore = {(SetOption(std::forward<ArgsT>(args)), 0)...};
    }

    const std::string_view &GetName() const
    {
        return _name;
    }

    const Helper::MemberPointerType<T> &GetValue(const Fields &fields) const
    {
        return fields.*_member;
    }

    Helper::MemberPointerType<T> &GetValue(Fields &fields)
    {
        return fields.*_member;
    }

    void SetOption(Desc description)
    {
        _description = std::move(description);
    }

    void SetOption(Sensitive)
    {
        _isSensitive = true;
    }

    void SetOption(Deprecated)
    {
        _isDeprecated = true;
    }

    const QString &Description() const
    {
        return _description.Description();
    }

    bool IsSensitive() const
    {
        return _isSensitive;
    }

    bool IsDeprecated() const
    {
        return _isDeprecated;
    }

private:
    std::string_view _name;
    T _member;
    Impl::Desc _description;
    bool _isSensitive{false}, _isDeprecated{false};
};

template <class T>
class BasicSafeAccessor
{
public:
    BasicSafeAccessor(std::mutex &lock, T &fields) : _lock{lock}, _fields{fields} {}
    BasicSafeAccessor(const BasicSafeAccessor &rhs) = delete;

    T *operator->()
    {
        return &_fields;
    }

protected:
    std::unique_lock<std::mutex> _lock;
    T &_fields;
};

// What a subscription stores: told about every change, decides for itself whether the
// field it cares about is among them.
using FieldWatcher =
    std::function<void(const Fields &previous, const Fields &current, bool initial)>;

} // namespace Impl

// Increase this value when the current ABI cannot be backward compatible
// For example, the name or type of an old key has changed
//
constexpr inline uint32_t kFieldsAbiVersion = 1;

struct MetaFields {
#define DECLARE_META_FIELD(type, name, dft, ...)                                                   \
    Impl::MetaField<type Fields::*> name{TO_STRING(name), &Fields::name, __VA_ARGS__};
    SETTINGS_FIELDS(DECLARE_META_FIELD)
#undef DECLARE_META_FIELD
};

inline const auto &GetConstMetaFields()
{
    static MetaFields i;
    return i;
}

//////////////////////////////////////////////////
// Subscribing to changes
//

//
// The lifetime of a `Watch`. The callback is never invoked again once this is destroyed
// or reset, so a subscriber that captures `this` keeps the handle as a member and is safe.
//
class Subscription
{
public:
    Subscription() = default;
    explicit Subscription(uint64_t id) : _id{id} {}
    ~Subscription();

    Subscription(Subscription &&other) noexcept : _id{std::exchange(other._id, 0)} {}
    Subscription &operator=(Subscription &&other) noexcept;
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;

    void Reset();

private:
    uint64_t _id{0};
};

namespace Impl {
Subscription Subscribe(FieldWatcher watcher);
}

//
// React to one field. The callback runs once with the current value when `Apply` is
// called, and after that only when the stored value actually changes.
//
// It runs on the thread that changed the setting, after the settings lock has been
// released -- so it may read or modify settings itself, and a consumer on another thread
// must hop to it (the `...Safely` signals in the GUI classes are that hop).
//
template <class T, class CallbackT>
Subscription Watch(T Fields::*field, CallbackT callback)
{
    return Impl::Subscribe([field, callback = std::move(callback)](
                               const Fields &previous, const Fields &current, bool initial) {
        if (initial || previous.*field != current.*field) {
            callback(current.*field);
        }
    });
}

//////////////////////////////////////////////////

LoadResult Load();

// Replaces every field, saves, and notifies watchers of the ones that changed.
void Save(Fields newFields);

// Notifies every watcher of the current value of its field. Called once after loading,
// so consumers start from what is stored rather than from their own defaults.
void Apply();

Fields GetCurrent();
Fields GetDefault();

using ConstSafeAccessor = Impl::BasicSafeAccessor<const Fields>;

//
// Write access. The change is saved and watchers are notified when the accessor goes out
// of scope, so keep it short-lived: `ModifiableAccess()->auto_run = true;`.
//
class ModifiableSafeAccessor : public Impl::BasicSafeAccessor<Fields>
{
public:
    using Impl::BasicSafeAccessor<Fields>::BasicSafeAccessor;

    ModifiableSafeAccessor(std::mutex &lock, Fields &fields);
    ~ModifiableSafeAccessor();

private:
    Fields _oldFields;
};

ModifiableSafeAccessor ModifiableAccess();

} // namespace Core::Settings
